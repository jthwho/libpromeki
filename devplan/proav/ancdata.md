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

The plan lands in dependency order: foundation types first
(format identity, transport identity, packet carrier, payload,
descriptor), then the ST 2110-40 wire format, then the two
registries (transport-to-transport translators and typed
codecs), then typed parsers for the first-class formats, then
MediaIO backend integration, then application surfaces. None of
this requires the AJA NTV2 backend — when that lands it slots
into the contract defined here.

## Status at a glance (2026-05-13)

| Phase | What | Status |
|------:|------|--------|
| 0     | Foundation types (AncFormat, AncPacket, AncDesc, AncPayload, AncMeta, St291Packet, HdmiInfoFrame, MediaDesc/Frame plumbing) | **Landed** — Phase 0 |
| 1     | RTP ST 2110-40 / RFC 8331 (RtpPayloadAnc + ANC packetizer/depacketizer threads + RtpMediaIO stream wiring) | **Landed** — Phase 1 |
| 1.5   | ESN reorder handling | Deferred (packer writes 0, depacketizer ignores) |
| 2     | `AncTranslateConfig` + `AncTranslator` + 3 registries + macros + initial ATC and AFD ← → St291 codecs | **Landed** — Phase 2 |
| 2b    | CEA-708 ← → St291 codec + `Cea708Cdp` typed value type + TPG caption injection + Inspector AncData JSONL dump | **Landed** — Phase 2b end-to-end slice (the realish use case) |
| 3     | Remaining typed parsers (CEA-608, AFD value type, Atc helpers, Scte104, HDR static/dynamic, KLV) | Pending |
| 3.5   | Subtitle file I/O + CEA-608 codec (generic `Subtitle` / `SubtitleList` value types + `SubRip` parser/emitter, Scc, `Cea608Encoder`/`Decoder`, TPG SubRip-driven injection, round-trip func test) | **Landed end-to-end (2026-05-13)** — `Subtitle`/`SubtitleList`/`SubRip` with structured `SubtitleSpan` runs + inline markup parse; `Cea608Encoder` + `Cea608Decoder` with **all three modes — pop-on, paint-on, roll-up** — and full PAC + mid-row attribute set (anchor row, italic, underline, 7-colour-quantised palette via `Color::nearestPaletteIndex`); TPG SubRip-driven injection (offset, snap-to-frame, per-frame `Metadata::Subtitle` stamp); encoder pre-roll filter (`encodableSubset`); **`Scc` value type + TPG SCC bypass path** (`TpgAncCaptionsScc` config key feeds SCC byte-pairs directly into cc_data, bypassing `Cea608Encoder` — proves the CDP wire layer independently of encoder scheduling); **full CEA-608 character set landed 2026-05-13** (`Cea608Ext` table covering the 10 remapped basic-G0 positions + 16 Special Characters + 64 Extended Western European glyphs); **CC2/CC3/CC4 channel support + per-cue mode mixing landed 2026-05-13** (every channel via a single channel-bit OR-mask post-pass; per-cue mode dispatch via `Subtitle::mode` + per-cue `Subtitle::rollUpRows`); **horizontal positioning + Cea608Packet Variant + BS/DER/FON landed 2026-05-13** (anchor's horizontal half drives PAC indent + doubled Tab Offset T1/T2/T3 — Center / Right cues land at the right column; coloured + indented cues degrade to flush-left preserving colour; decoder column-tracking recovers Left / Center / Right anchor; new `Cea608Packet` Variant value type carries channel + cc_data triples for per-channel app-level interchange; decoder honours BS / DER / FON misc codes for live-captioner typo-correction + row-clear + flash flag); **encoder/decoder production-readiness pass landed 2026-05-13** (`Cea608::CaptionColor::Black = 7` for the EIA-608-B BG-attribute round-trip with new `FgCaptionColorCount = 7` for fg-only test iteration; `isPac` mirrors `decodePac`'s row-11 b2-bit-5 constraint; `doRCL` finalises any in-flight paint-on / roll-up cue at the supplied ts before clearing; `doENM` gated to pop-on per spec; multi-row PAC handling resets per-row `loadingColumn` and commits anchor off the first row's column only); **Phase 3.5g `anc-subrip-roundtrip` functional test landed 2026-05-13** (as `promeki-test` case `captions.cea608.subrip_roundtrip` rather than `tests/func/anc-subrip-roundtrip/`) |
| 4     | MediaIO backend integration (NdiMediaIO ANC, RtmpMediaIO ANC, **`Cea708 ← / → HlsSei` codec + NVENC SEI injection** for RTMP/HLS/SRT delivery to YouTube etc., AncMetadataStamper) | **YouTube delivery path landed 2026-05-12 (working tree, uncommitted)** — `Cea708 ← / → HlsSei` codec, codec API Frame-shaped refactor + ANC pairing foundation, **plus the `NvencVideoEncoder` SEI injection body** (`MediaConfig::VideoSeiCaptionsEnabled` default-on, `selectAncForSei` walked per-submit, `AncTranslator → HlsSei` payload bytes stashed on the slot, NV_ENC_PIC_PARAMS H264/HEVC `seiPayloadArray` populated with `payloadType=4`).  Three new doctests verify the encoded H.264 bitstream contains the ATSC A/53 wrapper marker `0xB5 0x00 0x31 0x47 0x41 0x39 0x34 0x03` with the expected cc_count + cc_data triple bytes, plus negative cases for opt-out and no-ANC silence.  AV1 warns-once and emits no caption metadata (no NVENC AV1 caption-OBU path).  NdiMediaIO + RtmpMediaIO ANC + AncMetadataStamper still pending |
| 5     | AJA NTV2 SDI ingest contract (documentation only) | Pending |
| 6     | mediaio --dump-anc tool, TPG AncEmission mode, caption renderer, functional test matrix, docs, CEA-708 typed `Cea708Service` / `Cea708WindowState` decoder (DTVCC service-block + window manager) | **Mostly landed** — Inspector AncData JSON dump landed early as part of Phase 2b; caption renderer landed 2026-05-11 (`SubtitleSpan`, SubRip styled-spans, `FastFont` cache, `SubtitleRenderer`, `SubtitleBurnMediaIO`, `Cea608Decoder::displayedCue`); **full CEA-708 DTVCC stack landed 2026-05-12** — `Cea708Service` + `Cea708DtvccPacket` value types (cc_data round-trip, standard + extended block headers), `Cea708WindowState` (8-window × pen-state × character-buffer state machine consuming the full C0/G0/C1/G1/EXT1 byte stream), `Cea708Decoder` (cc_data triples → DTVCC packets → window state → SubtitleList), `Cea708Encoder` (SubtitleList → cc_data triples via DefineWindow + DisplayWindow); full encoder ↔ decoder round-trip exercised.  **TPG 708 emission landed 2026-05-12** — `CaptionCodec` typed enum + `TpgAncCaptionsCodec` / `TpgAncCaptions708Service` config keys; `TpgMediaIO` now drives `Cea608Encoder`, `Cea708Encoder`, or both side-by-side, with full Inspector AncData JSONL round-trip coverage.  **Full CEA-708 Unicode support landed 2026-05-13** — new `Cea708Ext` helper class with the CEA-708-D Annex G G2 table + the lone G3 ATSC CC logo entry; encoder walks codepoints (not bytes) and picks the cheapest wire encoding per codepoint (G0 single byte / G1 single byte / EXT1+G2 two bytes / EXT1+G3 two bytes / P16 three bytes / UTF-16 surrogate pair via two P16 sequences = six bytes for astral codepoints); decoder G2 / G3 lookup replaces the U+FFFD stub; `Cea708WindowState` carries a pending high-surrogate slot so a UTF-16 surrogate pair survives a `processBytes` boundary (e.g. across DTVCC packets).  Functional tests still pending |

The Phase 2b end-to-end slice proves the architecture: caption text
goes in via `MediaConfig::TpgAncCaptionsFile` (a SubRip `.srt` path,
Phase 3.5), rides through TPG → MediaIOPortConnection → Inspector,
and lands in a JSONL file with the original bytes recoverable from
the cc_data triples.  As of 2026-05-11 the same SRT path also drives
a *visual* render via `SubtitleBurnMediaIO`, which reads either the
TPG-stamped `Metadata::Subtitle` or decodes CEA-608 from the
frame's `AncPayloads` (selectable through
`VideoSubtitleBurnSources`).  As of 2026-05-12 the alternative
"real broadcast captioner output" path also works: an `.scc` file
fed via `MediaConfig::TpgAncCaptionsScc` bypasses the
`Cea608Encoder` entirely and rides directly into the `cc_data`
section, proving the CDP wire layer in isolation from the
encoder's scheduling decisions.  Also as of 2026-05-12, the full
**CEA-708 DTVCC stack** is in place: `Cea708Service` /
`Cea708DtvccPacket` for the wire layer, `Cea708WindowState` for
the 8-window state machine, plus matching `Cea708Encoder` /
`Cea708Decoder` that round-trip text through cc_data triples.
The ATSC A/53 **HlsSei codec** is registered alongside the St291
codec, so the same `AncTranslator` can shuttle CEA-708 between
SDI-shaped ST 291 packets and H.264 SEI for YouTube / HLS / SRT
delivery.

A **Frame-shaped codec API refactor** plus the **NVENC caption-SEI
injection body** are in the working tree (uncommitted as of
2026-05-12 end-of-day): `VideoEncoder` / `VideoDecoder` /
`AudioEncoder` / `AudioDecoder` now push and pull whole `Frame`
objects rather than bare payloads, with `selectInputPayload` /
`buildOutputFrame` helpers on each base and a new
`VideoEncoder::selectAncForSei(frame, pairedVideoStreamIndex,
allowedFormats)` helper on the encoder side +
`VideoDecoder::attachExtractedAnc(frame, pkt, pairedVideoStreamIndex)`
on the decoder side.  `AncDesc` was rebuilt as an internally-CoW
value-type handle (matching the post-2026-05-07 convention used by
`Buffer`, `Frame`, `Metadata`, `AncPacket`) and now carries
`pairedVideoStreamIndex` / `pairedAudioStreamIndex` so an ANC stream
can be attributed to a specific encoded video/audio essence on the
enclosing Frame.  TPG already stamps the pairing (video stream 0).
**NVENC** now consumes that pairing: gated by
`MediaConfig::VideoSeiCaptionsEnabled` (default-on, silent-no-op
when no matching ANC is present), `submitFrame` walks the source
Frame's ANC, translates each CEA-708 packet via the held
`AncTranslator` to `AncTransport::HlsSei`, and stashes the bytes on
the slot for `NV_ENC_PIC_PARAMS_H264 / _HEVC::seiPayloadArray` to
pick up.  This closes the YouTube / Twitch / RTMP+H.264 caption
delivery path end-to-end at the unit-test layer (TPG SubRip → NVENC
H.264 → ATSC A/53 SEI bytes recoverable from the NAL stream).
Multi-packet CDPs (CDP > 255 bytes), NDI / RTMP-AMF ANC codecs,
the `AncMetadataStamper` pipeline stage, and a real over-the-wire
RTMP / HLS functional test are the remaining gaps.

## Decisions

These are settled scope answers that the rest of this plan
assumes — captured here so reviewers don't re-litigate them
mid-implementation.

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
  `RtmpAmfObjectName`).  Mirrors the `MediaConfig` pattern: a
  `VariantDatabase<"AncTranslateConfig">` subclass with typed-key
  statics on the class.  The config is passed at `AncTranslator`
  construction time and threaded internally to every
  parser/builder/translator handler the instance dispatches to;
  per-call public APIs are cfg-free.  String round-trip is JSON
  via the inherited `VariantDatabase` machinery + `Metadata::toString`
  / `fromString` (prerequisite addition that landed in Phase 0).

  **Naming drift from the original sketch (intentional).**  The
  devplan originally proposed `St291::BuildLine` style nested
  namespaces; these are not representable as a single C++
  identifier in a flat `VariantDatabase` key registry, so the
  C++ identifier collapses the namespacing dot
  (`St291BuildLine`) while the wire-name keeps it
  (`St291.BuildLine`) for readable JSON output.  Functionally
  identical.

  **Variant integration deferred.**  `TypeAncTranslateConfig` is
  not in the Variant X-macro (same circular-include reason as
  `TypeAncPacket` — `AncTranslateConfig` inherits
  `VariantDatabase`, which already pulls in `variant.h`).  Not
  load-bearing: the config rides through DataStream via the
  template `VariantDatabase` operators and through JSON strings
  via `toString` / `fromString`.
- **First-class ANC formats (typed parsers/builders):**
  - SMPTE 334-2 CEA-708 CDP — **landed** (Phase 2b: full
    `Cea708Cdp` value type with cc_data triples, timecode
    section, opaque extras pass-through, JSON dump, Variant
    payload type `TypeCea708Cdp` = tag 0x5B).
  - SMPTE 12M-2 ATC LTC / VITC1 / VITC2 timecode — **landed**
    (Phase 2: codec round-trips through the existing
    `Timecode` value type).
  - SMPTE 2016 AFD — **landed** (Phase 2: minimal `uint8_t`
    Variant carrying AFD code + AR bit; the full `Afd` value
    type with Bar Data is still a Phase 3 task).
  - CEA-608 — covered structurally by `Cea708Cdp`'s cc_data
    triples (cc_type=0/1).  A dedicated CEA-608 value type
    layer is a Phase 3 task.
  - SCTE-104 ad/program markers (with SCTE-35 as a separate
    format for MPEG-TS carriage) — Phase 3 pending.
  - SMPTE 2086 static HDR metadata — Phase 3 pending.
  - SMPTE ST 2094-40 dynamic HDR (HDR10+) — Phase 3 pending.
  - MISB ST 0601 KLV geolocation — Phase 3 pending.
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
  `AncTransport`, `AncTranslator`). Transport
  helpers are named for their transport (`St291Packet`,
  `HdmiInfoFrame`). Typed parsers are named for their format
  (`Cea708Cdp`, `Afd`, `Atc`, `Scte104`, `HdrStatic2086`,
  `HdrDynamic2094_40`, `Klv0601`). No type spells out
  "Ancillary" in full.

## Architecture overview

The stack has four layers; nothing in a lower layer depends on
a higher one.

```
┌─────────────────────────────────────────────────────────────┐
│ Layer 4 — Typed parsers / builders                          │
│   Cea708Cdp [landed]                                        │
│   Afd, Atc, Scte104, HdrStatic2086, HdrDynamic2094_40,      │
│   Klv0601, AncMetadataStamper [pending]                     │
│   "Give me the timecode / caption / splice as a Variant."   │
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
│   translate() prefers a direct translator and falls back    │
│   to parse + build automatically.                           │
│   Codecs landed: Cea708, AtcLtc, AtcVitc1, AtcVitc2, Afd    │
│                  (all on transport St291).                  │
└──────────────────────────────┬──────────────────────────────┘
                               │ operates on
┌──────────────────────────────▼──────────────────────────────┐
│ Layer 2 — Transport helpers [landed]                        │
│   St291Packet, HdmiInfoFrame                                │
│   Typed accessors over (data, meta) when transport matches. │
│   Implicit decay to const AncPacket &; explicit promotion   │
│   from AncPacket via from().                                │
└──────────────────────────────┬──────────────────────────────┘
                               │ holds
┌──────────────────────────────▼──────────────────────────────┐
│ Layer 1 — Generic carrier + identity types [landed]         │
│   AncPacket      — (AncFormat, AncTransport, Buffer, Meta)  │
│   AncPayload     — MediaPayload subclass; AncPacket::List   │
│   AncDesc        — per-stream descriptor                    │
│   AncFormat      — TypeRegistry of "what kind"              │
│   AncTransport   — enum of "where it rides"                 │
│   AncCategory    — enum of broad classification             │
│   AncMeta        — well-known per-transport meta keys       │
└─────────────────────────────────────────────────────────────┘

Producer side (landed):
  TpgMediaIO ─[TpgAncCaptionsEnabled / TpgAncCaptionsFile (.srt)
               / TpgAncCaptionsOffset]→
    SubRip::parse → SubtitleList::snapToFrames →
    Cea608Encoder::encodableSubset (drops cues whose pre-roll
                                    overlaps t=0 or prior EDM,
                                    warns once per dropped cue)
    Cea608Encoder.nextFrame (emits PAC from cue anchor + first
                             span style; mid-row codes for span
                             style changes; 7-colour-quantised
                             palette; doubled control codes per
                             spec) → Cea708Cdp →
    AncTranslator.build → AncPayload → Frame
    (active Subtitle re-stamped on Frame's Metadata::Subtitle on
     every frame in the cue's display window so renderers and
     other consumers can pull it without tracking state)

Burn-in side (landed):
  SubtitleBurnMediaIO ─[VideoSubtitleBurnSources (ordered
                       EnumList<SubtitleSource>: Metadata /
                       Cea608Anc)]→
    For each frame:
      Source 1 = Metadata::Subtitle on the Frame
      Source 2 = Cea608Decoder.pushFrame(ancPayloads' Cea708Cdp
                 cc_data) → displayedCue() returning a styled
                 Subtitle (anchor recovered from PAC row, spans
                 carry italic / underline / palette colour)
    First non-empty source wins → SubtitleRenderer.render
      (FastFont with multi-key glyph cache → italic /
       underline / palette colour switches don't drop the
       cache; bold emits a one-shot warn — 608 wire can't
       carry bold)

Inspector side (landed):
  InspectorMediaIO ─[InspectorTest::AncData]→
    Frame.ancPayloads → AncTranslator.parse → Cea708Cdp.toJson →
    JSONL row to file (InspectorAncDataFile)
```

**Lifecycle of one ANC packet**, end-to-end:

1. **Ingress.** Backend receives wire bytes (NDI XML element,
   RTP-40 ST 291 packet, RTMP AMF script tag, …). Wraps them
   in an `AncPacket` with the matching `AncTransport` and the
   matching `AncFormat` (looked up via the registry's
   per-transport key — `fromSt291DidSdid`, `fromHdmiInfoFrameType`,
   etc.). Per-packet framing metadata (line, h-offset, script
   name, …) goes into `meta`.
2. **Pipeline.** The `AncPacket` rides through `MediaPipeline`
   inside the frame's `AncPayload`. Copies are refcount bumps;
   no decode, no translation.
3. **Inspection (optional).** Application code or
   `AncMetadataStamper` calls `Cea708Cdp::parse(pkt)` /
   `Atc::parse(pkt)` / etc. The typed parser delegates to an
   `AncTranslator::parse(pkt)` call, which dispatches to the
   registered parser for `(pkt.format(), pkt.transport())` —
   works whether the packet is St291, NdiXml, or any
   transport with a registered parser.
4. **Egress.** Sink backend holds an `AncTranslator` and
   walks the payload. For each packet: if `pkt.transport()
   == sink's native transport`, emit `pkt.data()` verbatim
   (byte-exact). Otherwise call
   `translator.translate(pkt, sinkT)` — the translator
   prefers a registered direct `(fmt, src, dst)` handler and
   falls back to `parse(pkt) → build(variant, fmt, dst)` when
   none exists. Packets with no parser or builder for the
   needed pair are skipped and logged once per format.

---

## Phase 0 — Foundation types

The minimum substrate the rest of the plan depends on. No wire
formats yet; no backend integration yet.

**Status:** Landed 2026-05-11.  Full library + tests build clean;
95 new ANC / Metadata test cases (494 assertions) all pass.  Two
items were deliberately deferred (with rationale captured inline
below):

- `AncDesc` SDP `fromSdp` / `toSdp` — moved to Phase 1, which is
  where the matching `RtpPayloadAnc` and the SDP `m=application`
  section land.  The DataStream round-trip on `AncDesc` ships now.
- `AncPacket` `TypeAncPacket` Variant integration — `AncPacket::Impl`
  holds a `Metadata`, which transitively depends on `Variant`
  itself, so adding `AncPacket` to the `Variant` X-macro creates a
  circular include.  `AncPacket` flows through `AncPayload`'s
  `AncPacket::List` (not via `Variant`), so the integration is not
  load-bearing for the Phase 0 contract.  The DataStream tag
  `TypeAncPacket` still ships for direct serialization.

The Phase 0 test path is `tests/unit/<name>.cpp` (the library's
flat test layout) rather than the `tests/unit/proav/<name>.cpp`
nesting the original plan suggested — the latter directory does
not exist in the test tree.

### Prerequisite: `Metadata::toString` / `fromString`

`AncTranslateConfig` (Phase 2) needs JSON string round-trip
through its backing `Metadata`. Neither
`Metadata::toString` (returning a JSON-encoded `String`) nor
`Metadata::fromString` exists today; both must be added before
Phase 2 can build, so they land here as small additive
changes.

**Files:**
- [x] `include/promeki/metadata.h` — declarations.
- [x] `src/core/metadata.cpp` — implementations delegating to
  the existing `JsonObject` ↔ `VariantDatabase` plumbing
  (`Metadata` is a `VariantDatabase` subclass).
- [x] `tests/unit/metadata.cpp` — new doctests covering
  the new round-trip surface (existing tests stay).

**Surface:**
- [x] `String toString(unsigned int indent = 0) const` —
  serializes the metadata to JSON via the same path
  `JsonObject::fromVariantDatabase` / `JsonObject::toString`
  already use.  Indentation parameter added so pretty / compact
  forms share one entry point.
- [x] `static Metadata fromString(const String &,
  Error *err = nullptr)` — inverse; sets `err` on malformed
  input and returns a default-constructed `Metadata`.

**Doctests:**
- [x] Empty `Metadata::toString` produces `"{}"` (or the
  registered canonical empty JSON form).
- [x] Set known well-known keys (string, integer, bool, enum,
  nested), round-trip to/from string, compare equal to
  original.
- [x] Malformed JSON sets `err` and returns empty metadata.
- [x] Indented round-trip — `fromString(toString(2)) == m`.

### `AncCategory` (typed enum)

Broad classification of an `AncFormat`. Drives sink filtering
("I carry Captions only"), inspector grouping, and metadata
stamping rules.

**File:** entry in `include/promeki/enums.h`
(`TypedEnum<AncCategory>` per the `feedback_typedenum_enums_h`
rule).

**Values:**
- [x] `Unknown = 0`
- [x] `Captions` — CEA-708, CEA-608
- [x] `Timecode` — ATC LTC/VITC, future HDMI/NDI timecode
- [x] `Splice` — SCTE-104, SCTE-35
- [x] `Aspect` — AFD, Bar Data, AVI InfoFrame (aspect bits)
- [x] `Hdr` — SMPTE 2086 static metadata, HDR10+ dynamic
  metadata (ST 2094-40), Dolby Vision RPU
- [x] `AudioMetadata` — SMPTE 2020 Dolby metadata,
  HDMI Audio InfoFrame
- [x] `Display` — non-HDR display hints (HDMI AVI/SPD
  InfoFrames)
- [x] `Geolocation` — MISB ST 0601 KLV and friends
- [x] `UserDefined`

### `AncTransport` (typed enum)

Names the wire carrier a packet is in. Distinct from
`AncFormat` (the logical "what kind of data is this"):
`AncFormat::Cea708` is closed-caption content regardless of
whether it's currently riding inside an ST 291 packet, an NDI
XML metadata frame, or an AMF0 script tag.

**File:** entry in `include/promeki/enums.h`
(`TypedEnum<AncTransport>`).

**Values:**
- [x] `Invalid = 0`
- [x] `St291` — ST 291 ancillary packet (SDI VANC/HANC and
  RFC 8331 ST 2110-40 both consume this transport directly).
- [x] `NdiXml` — NDI metadata frame body (UTF-8 XML).
- [x] `RtmpAmf` — RTMP AMF0 script tag value.
- [x] `HdmiInfoFrame` — HDMI InfoFrame body.
- [x] `MpegTsPrivate` — MPEG-TS private section / table
  (SCTE-35 splice_info, KLV, ARIB).
- [x] `HlsSei` — H.264 / HEVC SEI user-data registered
  message (CEA-708 NAL fragments for HLS muxers).

### `AncFidelity`, `AncChecksumPolicy`, `AncOnUnsupported` (typed enums)

Knobs driving translator/codec behaviour. Each is a
`TypedEnum<…>` entry in `include/promeki/enums.h` so it's
Variant-able on its own (rides through MediaConfig and
DataStream without extra plumbing). The aggregate config
object that bundles them is `AncTranslateConfig` (Phase 2).

**`AncFidelity`** — output verbosity for translators emitting
to a transport that has multiple valid representations
(XML / JSON / AMF mostly):
- [x] `Default = 0` — translator's pick (normally equivalent
  to `Strict`).
- [x] `Strict` — minimum required fields for a valid emit.
- [x] `Full` — every optional field, most verbose form.

**`AncChecksumPolicy`** — how `St291Packet`-producing
translators handle the ST 291 checksum:
- [x] `PreserveOrRecompute = 0` — use the stored checksum
  when it's valid; recompute when missing or wrong. Default;
  preserves byte-exact replay for captured packets.
- [x] `AlwaysRecompute` — recompute on every output.
- [x] `StrictValidate` — fail with `Error::InvalidChecksum`
  when the stored checksum doesn't match the recomputed one.

**`AncOnUnsupported`** — what to do when the input can't be
represented in the target:
- [x] `Skip = 0` — return `Error::NotSupported`; caller (sink
  backend) drops the packet and logs once.
- [x] `BestEffort` — emit what we can, return OK. Default.
- [x] `Fail` — hard error with a descriptive message.

### `AncMeta` (well-known metadata keys)

Compile-time constants for the per-transport metadata sidecar
that lives in `AncPacket::meta()`. One header so misspelled
keys are caught at compile time.

**File:**
- [x] `include/promeki/ancmeta.h` — nested namespaces (one per
  transport) of `inline const Metadata::ID` key constants
  declared via `Metadata::declareID`.  Naming pattern is
  `AncMeta::<Transport>::<Key>`; wire-side names are
  `"AncMeta.<Transport>.<Key>"`.  (The "inline constexpr"
  language in the original plan was a near miss — the IDs
  are registered values that need a `Metadata::declareID`
  call, so `inline const` works but `constexpr` doesn't.)

**Initial keys** (shown unqualified for brevity; full names
are `AncMeta::<Transport>::<Key>`, e.g. `AncMeta::St291::Line`.
The rest of this doc abbreviates the same way):
- [x] `St291::Line` (uint16_t), `St291::HOffset` (uint16_t),
  `St291::FieldB` (bool), `St291::CBit` (bool),
  `St291::StreamNum` (uint8_t).
- [x] `HdmiInfoFrame::Type` (uint8_t — 0x82 AVI, 0x84 Audio,
  0x83 SPD, 0x87 DRM, 0x81 Vendor, …),
  `HdmiInfoFrame::Version` (uint8_t),
  `HdmiInfoFrame::Length` (uint8_t).
- [x] `RtmpAmf::ScriptName` (String — `onCaptionInfo`,
  `onCuePoint`, `onMetaData`, …).
- [x] `NdiXml::ElementName` (String — top-level XML
  element for fast filtering).
- [x] `MpegTsPrivate::Pid` (uint16_t),
  `MpegTsPrivate::TableId` (uint8_t).
- [x] `HlsSei::PayloadType` (uint8_t — typically 5),
  `HlsSei::Uuid` (UUID — for unregistered SEI).

### `AncFormat` (TypeRegistry-style format registry)

A lightweight inline wrapper around an immutable `Data`
record identified by an integer `ID`, parallel in every
respect to `PixelFormat` and `AudioFormat`. Well-known IDs
ship with the library; user code can register additional
formats at runtime via `registerType()` + `registerData()`.

**Files:**
- [x] `include/promeki/ancformat.h`
- [x] `src/proav/ancformat.cpp`
- [x] `tests/unit/ancformat.cpp`

**Built-in IDs (initial set; grows as parsers land).** The
"canonical" transport column is the format's primary wire
representation; the per-transport key is what the registry's
`from<Transport>...` lookup matches against.

| ID | Name              | Canonical T   | Key                       | Notes                       |
|----|-------------------|---------------|---------------------------|-----------------------------|
| 0  | `Invalid`         | `Invalid`     | —                         | default                     |
| 1  | `Cea708`          | `St291`       | DID 0x61, SDID 0x01       | SMPTE 334-2 CDP             |
| 2  | `Cea608`          | `St291`       | DID 0x61, SDID 0x02       | SMPTE 334-1                 |
| 3  | `Afd`             | `St291`       | DID 0x41, SDID 0x05       | SMPTE 2016-3                |
| 4  | `BarData`         | `St291`       | DID 0x41, SDID 0x06       | SMPTE 2016-3                |
| 5  | `Scte104`         | `St291`       | DID 0x41, SDID 0x07       |                             |
| 6  | `Scte35`          | `MpegTsPrivate` | TableId 0xFC            | TS splice_info section      |
| 7  | `AtcLtc`          | `St291`       | DID 0x60, SDID 0x60       | SMPTE 12M-2                 |
| 8  | `AtcVitc1`        | `St291`       | DID 0x60, SDID 0x61       |                             |
| 9  | `AtcVitc2`        | `St291`       | DID 0x60, SDID 0x62       |                             |
| 10 | `Smpte2020Audio`  | `St291`       | DID 0x45, SDID range      | see sub-type note below     |
| 11 | `HdrStatic2086`   | `HdmiInfoFrame` | InfoFrameType 0x87 (DRM) | also has St291 codec        |
| 12 | `HdrDynamic2094_40` | `HdmiInfoFrame` | InfoFrameType 0x81 (Vendor, HDR10+ OUI) | also has St291 codec |
| 13 | `DvRpu`           | `HdmiInfoFrame` | InfoFrameType 0x81 (Vendor, DV OUI) |                  |
| 14 | `AviInfoFrame`    | `HdmiInfoFrame` | InfoFrameType 0x82      |                             |
| 15 | `AudioInfoFrame`  | `HdmiInfoFrame` | InfoFrameType 0x84      |                             |
| 16 | `SpdInfoFrame`    | `HdmiInfoFrame` | InfoFrameType 0x83      |                             |
| 17 | `VendorInfoFrame` | `HdmiInfoFrame` | InfoFrameType 0x81      | catchall when OUI not recognised |
| 18 | `Klv0601`         | `St291`       | DID 0x44, SDID 0x04       | also has MpegTsPrivate codec; MISB ST 0601 |
| 1024 | `UserDefined` (first available) | — | —      | runtime registrations       |

**Smpte2020Audio sub-types.** SMPTE 2020 uses DID 0x45 across
SDIDs 0x01–0x09 to carry different Dolby-metadata flavours.
Modelled initially as one `Smpte2020Audio` ID — registered
with `st291Did = 0x45` and a wildcard `st291Sdid = 0` so
`fromSt291DidSdid(0x45, X)` for `X ∈ {0x01..0x09}` returns
the same `Smpte2020Audio` format. The SDID byte sits in the
packet's wire bytes and is recovered via `St291Packet::sdid()`
when a caller needs to discriminate sub-flavours; the typed
parser for `Smpte2020Audio` exposes the SDID on the parsed
Variant. Promoted to separate IDs only if a downstream
consumer needs to filter at the `AncFormat::ID` level.

**Vendor-specific HDMI InfoFrames.** `HdrDynamic2094_40`,
`DvRpu`, and `VendorInfoFrame` all live on InfoFrameType 0x81;
the discriminator is the 3-byte OUI inside the body. The
registry's `fromHdmiInfoFrameType` lookup returns
`VendorInfoFrame` for 0x81 generically; consumers that decode
the OUI and want the specific ID call `AncFormat::byNameOrId`
explicitly. Codec implementations recognise the OUI in
`decode()` and re-tag the packet's `format()` to the specific
ID at parse time.

**`AncFormat::Data` (immutable record):**
- [x] `ID id`
- [x] `String name` — short canonical name
  (e.g. `"Cea708"`); resolves through `fromName`.
- [x] `String desc` — human-readable description.
- [x] `AncCategory category` — broad classification.
- [x] `AncTransport canonicalTransport` — nominal "where
  this format lives natively" hint. Used by builders /
  test-pattern sources when the caller passes only a format
  identifier.
- [x] `uint8_t st291Did`, `uint8_t st291Sdid` — for formats
  with an ST 291 representation. Zero/zero means "no ST 291
  carriage."  A non-zero @c st291Did paired with
  @c st291Sdid==0 means "wildcard SDID" (the
  @c Smpte2020Audio case).
- [x] `uint8_t hdmiInfoFrameType` — for formats with an HDMI
  InfoFrame representation. Zero means "no HDMI carriage."
- [x] `uint8_t mpegTsTableId` — for formats with an MPEG-TS
  private-section representation. Zero means "no MPEG-TS
  carriage."
- (No `subType` field — sub-discriminators like the
  SMPTE 2020 SDID byte are per-packet, not per-format-ID,
  and are read from the packet's wire bytes via
  `St291Packet::sdid()` or from a parser/builder's typed
  Variant output. Keeping `AncFormat::Data` as a pure
  identity record keeps the registry lookups simple.)

**Class surface:**
- [x] `AncFormat()` — invalid format.
- [x] `AncFormat(ID id)` — resolves to the registered `Data`
  for `id`; invalid if not registered.
- [x] `explicit AncFormat(const String &name)` — name
  lookup, mirroring `PixelFormat`.
- [x] `static ID registerType()` — allocates a new ID at
  `UserDefined` or above.
- [x] `static void registerData(Data &&)` — installs an
  immutable record.
- [x] `static IDList registeredIDs()` — every registered
  ID, well-known and user. Required for the `using IDList`
  TypeRegistry convention.
- [x] `static AncFormat fromSt291DidSdid(uint8_t did,
  uint8_t sdid)` — "what is this raw ST 291 packet";
  exact match first, falls back to wildcard SDID-0 entry
  under the same DID, returns invalid otherwise.
- [x] `static AncFormat fromHdmiInfoFrameType(uint8_t type)`
  — "what is this raw HDMI InfoFrame"; returns the format
  registered for that InfoFrame type byte. For type 0x81
  returns `VendorInfoFrame` (the OUI-agnostic catchall);
  callers that decode the OUI promote to `HdrDynamic2094_40`
  / `DvRpu` / etc. via `AncFormat(id)`.
- [x] `static AncFormat fromMpegTsTableId(uint8_t tableId)`
  — "what is this MPEG-TS private section."
- [x] `static Result<AncFormat> fromName(const String &)` and
  `static AncFormat::ID idFromName(const String &)`.
  (`fromName` returns `Result<AncFormat>` rather than the
  plain `AncFormat` the original plan suggested, matching the
  `Result`-returning convention used by
  `AudioFormat::lookup`.)
- [x] `static IDList registeredIDsForCategory(AncCategory)` —
  every format in a category. Drives sink filtering.
- [x] `static IDList registeredIDsForTransport(AncTransport)`
  — every format whose `canonicalTransport` matches *or* that
  declares a non-zero key for that transport (St291 DID/SDID,
  HDMI type, MpegTs table-id). Drives the
  `AncDesc::allowedFormats` SDP fmtp emission and similar
  filters.
- [x] `ID id() const`, `const String &name() const`,
  `const String &desc() const`,
  `const AncCategory &category() const`,
  `const AncTransport &canonicalTransport() const`,
  `uint8_t st291Did() const`, `uint8_t st291Sdid() const`,
  `uint8_t hdmiInfoFrameType() const`,
  `uint8_t mpegTsTableId() const`,
  `bool isValid() const`.  (No `subType()` accessor — the
  field was dropped.)
- [x] `bool operator==(AncFormat) const`, `operator!=`.
- [x] `DataStream` operators (writes the registered name as a
  length-prefixed `String`, reads back via `idFromName`).
  Implementation lives in `datastream.cpp` (new
  `writeAncFormatData` / `readAncFormatData` helpers plus the
  member `operator<<` / `operator>>` overloads).  Tag value
  `TypeAncFormat = 0x58`.
- [x] Variant type integration: `TypeAncFormat` Variant type
  so `Metadata`/`VariantDatabase` can carry an `AncFormat`
  natively without string conversion.

**Doctests:**
- [x] Construction by ID, by name, default invalid.
- [x] `registerType` returns unique IDs that don't collide
  with the well-known range.
- [x] `registerData` installs and is retrievable.
- [x] `fromSt291DidSdid` for every St291-canonical format plus
  an unregistered pair (returns invalid).  Also covers the
  `Smpte2020Audio` wildcard SDID match across SDIDs 0x01–0x09.
- [x] `fromHdmiInfoFrameType` for every HDMI-canonical format
  plus an unregistered type (returns invalid).
- [x] `fromMpegTsTableId` for `Scte35` plus an unregistered
  table-id.
- [x] `registeredIDs` enumerates the full set.
- [x] `registeredIDsForCategory(Captions)` returns
  `{Cea708, Cea608}`; also covers `Hdr`.
- [x] `registeredIDsForTransport(St291)` returns every format
  with a non-zero `(st291Did, st291Sdid)`; analogous for the
  HDMI and MPEG-TS variants.
- [x] DataStream + Variant round-trip.

### `AncPacket` (CoW value type, the generic carrier)

The one type that flows through the pipeline. Always
`(AncFormat, AncTransport, Buffer data, Metadata meta)`,
internally CoW so list-of-values is cheap. No public
inheritance hierarchy; transport-specific access is provided
by composition helpers (`St291Packet`, `HdmiInfoFrame`, …).

**Files:**
- [x] `include/promeki/ancpacket.h`
- [x] `src/proav/ancpacket.cpp`
- [x] `tests/unit/ancpacket.cpp`

**Layout:**
- [x] `class AncPacket { ... };` with private
  `SharedPtr<Impl> _d;`.  No `::Ptr` alias (deleted, per the
  post-2026-05-07 convention).
- [x] `using List = ::promeki::List<AncPacket>;`.
- [x] Nested `struct Impl { ... };` declared in the header
  (carries `PROMEKI_SHARED_FINAL(Impl)` plus the four fields).
  The original plan suggested a PImpl with `Impl` defined in
  the `.cpp`, but the four-field layout (AncFormat,
  AncTransport, Buffer, Metadata) is short enough that
  exposing `Impl` saves a layer without leaking anything the
  header didn't already pull in.

**Class surface:**
- [x] `AncPacket()` — invalid (format=Invalid,
  transport=Invalid, empty data, empty meta).
- [x] `AncPacket(const AncFormat &fmt, const AncTransport &tr,
  Buffer data, Metadata meta = Metadata())` — full-state
  constructor.  Returns / takes the format / transport by
  `const &` rather than by value, matching how `AncFormat`
  and `AncTransport` are passed elsewhere.
- [x] Copy / move: refcount bump on the shared Impl;
  `std::move` stays cheap.
- [x] `const AncFormat &format() const`,
  `const AncTransport &transport() const`,
  `const Buffer &data() const`,
  `const Metadata &meta() const`.
- [x] `void setFormat(const AncFormat &)`,
  `void setTransport(const AncTransport &)`,
  `void setData(Buffer)`,
  `void setMeta(Metadata)` — each does CoW detach.
- [x] `Buffer &dataMut()`, `Metadata &metaMut()` — CoW detach
  then return mutable ref.  "Don't hold the reference across
  another copy of the packet" caveat documented.
- [x] `bool isValid() const` — `format.isValid() &&
  transport != AncTransport::Invalid`.
- [x] `bool operator==(const AncPacket &) const` and `!=`.
  Falls back to field-wise comparison only after the
  `SharedPtr<Impl>` identity short-circuit, so packets that
  alias the same impl compare equal in O(1).
- [x] `DataStream` operators: writes a `TypeAncPacket = 0x59`
  tag followed by format / transport / data / meta in that
  order.  `writeAncPacketData` / `readAncPacketData` free
  functions expose the untagged form so the Variant read
  dispatcher (once `AncPacket` is added) can call them
  without re-reading the tag.
- (No `TypeAncPacket` Variant integration: `AncPacket::Impl`
  holds a `Metadata`, which transitively depends on `Variant`
  itself.  Adding `AncPacket` to the `Variant` X-macro creates
  a circular include via `variant.h → ancpacket.h →
  metadata.h → variantdatabase.h → Variant`.  `AncPacket`
  flows through `AncPayload`'s `AncPacket::List`, not through
  `Variant`, so the integration is not load-bearing for the
  Phase 0 contract.  The DataStream tag `TypeAncPacket` still
  ships for direct (non-Variant) serialization of an
  `AncPacket` value.)
- [x] String-style `toString(verbose=false)` for diagnostics
  (format name, transport name, byte count; verbose form
  appends every meta key/value pair).

**Doctests:**
- [x] Default-invalid construction.
- [x] Full-state value construction (with and without meta).
- [x] CoW: mutating one copy via `setFormat` /
  `setTransport` / `setData` / `setMeta` / `metaMut` does
  not affect another held copy of the packet.
- [x] DataStream round-trip preserves format / transport /
  data bytes / meta (compared field-wise because
  `Buffer::operator==` is identity-based).
- [x] Equality compares every field; distinct `Buffer`
  instances with identical bytes compare unequal.
- [x] `toString` summarises format / transport / byte count;
  invalid handle stringifies as `"Invalid"`.

### `St291Packet` (transport helper, composition)

Typed view over an `AncPacket` whose transport is `St291`.
Holds an `AncPacket _pkt;` by value (cheap — one refcount).
Implicit decay to `const AncPacket &` for storage paths.

**Files:**
- [x] `include/promeki/st291packet.h`
- [x] `src/proav/st291packet.cpp`
- [x] `tests/unit/st291packet.cpp`

**Surface:**
- [x] `static Result<St291Packet> from(const AncPacket &)` —
  validates `transport == St291` and a minimum 5-byte data
  buffer; otherwise returns `Error::InvalidArgument`.
- [x] `static St291Packet build(const AncFormat &fmt,
  const List<uint16_t> &udw, uint16_t line,
  uint16_t hOffset = UnspecifiedHOffset (0xFFF),
  bool fieldB = false, bool cBit = false,
  uint8_t streamNum = 0)` — common path.  Delegates to
  `buildRaw` with the format's DID/SDID; wildcard-SDID
  formats (`Smpte2020Audio`) must use `buildRaw` so the
  caller can supply the discriminating SDID byte.
- [x] `static St291Packet buildRaw(uint8_t did, uint8_t sdid,
  const List<uint16_t> &udw, uint16_t line,
  uint16_t hOffset = UnspecifiedHOffset, bool fieldB = false,
  bool cBit = false, uint8_t streamNum = 0)` — escape hatch
  for unregistered DID/SDID pairs.  Looks the format up via
  `fromSt291DidSdid(did, sdid)` (may be `Invalid`); the
  resulting packet still rides through the pipeline and
  re-emits byte-exact.
- [x] Accessors that read out of `_pkt.data()` and
  `_pkt.meta()`:
  - `uint8_t did() const`, `uint8_t sdid() const`
  - `uint8_t dataCount() const`
  - `List<uint16_t> udw() const` — unpacked 10-bit words
    (data byte in bits 0–7, parity in 8–9)
  - `uint16_t checksum() const` — as-stored
  - `uint16_t computedChecksum() const` (per ST 291 §6.4)
  - `bool checksumValid() const`
  - `uint16_t line() const`, `uint16_t hOffset() const`,
    `bool fieldB() const`, `bool cBit() const`,
    `uint8_t streamNum() const` — all from `meta`
  - `bool isType1() const` — high bit of DID set
- [x] Mutators: `setUdw(const List<uint16_t> &)`,
  `setLine(uint16_t)`, `setHOffset(uint16_t)`,
  `setFieldB(bool)`, `setCBit(bool)`,
  `setStreamNum(uint8_t)`.  `setUdw` rebuilds the wire bytes
  and recomputes the checksum; the others stamp the meta
  sidecar.  Each rewrites under the hood via
  `_pkt.dataMut()` / `_pkt.metaMut()` (CoW detach).
- [x] `const AncPacket &packet() const`,
  `operator const AncPacket &() const`.
- [x] `bool isValid() const` — transport check plus the
  minimum-data-size sanity check.

**Doctests:**
- [x] Build with every well-known St291 format
  (Cea708 / Cea608 / Afd / BarData / Scte104 / AtcLtc /
  AtcVitc1 / AtcVitc2 / Klv0601).
- [x] `buildRaw` with the `Smpte2020Audio` wildcard SDIDs
  (0x45, 0x01..0x09).
- [x] `buildRaw` with an unregistered (did, sdid) — the
  wrapper format is `Invalid` but the wire bytes round-trip.
- [x] `from()` rejects wrong transport / too-short data.
- [x] Checksum compute / validate; `checksum ==
  computedChecksum` on freshly built packets.
- [x] UDW pack/unpack at every byte alignment modulo
  (DataCount values 1..8 cover two full 10-bit×4-word wraps).
- [x] `setUdw` replaces the payload and recomputes the
  checksum; meta mutators round-trip.
- [x] `isType1` reflects the high bit of DID.
- [x] Implicit decay to `const AncPacket &` accepted by
  AncPacket-taking functions and by `AncPacket::List`.

### `HdmiInfoFrame` (transport helper, composition)

Same pattern as `St291Packet` but for HDMI InfoFrame
transport. Sized small so it can ship now, with the actual
HDMI capture wiring deferred until a DeckLink HDMI MediaIO
or similar lands.

**Files:**
- [x] `include/promeki/hdmiinfoframe.h`
- [x] `src/proav/hdmiinfoframe.cpp`
- [x] `tests/unit/hdmiinfoframe.cpp`

**Surface:**
- [x] `static Result<HdmiInfoFrame> from(const AncPacket &)`
  — validates `transport == HdmiInfoFrame` and a minimum
  4-byte data buffer (the header is mandatory).
- [x] `static HdmiInfoFrame build(const AncFormat &fmt,
  uint8_t version, Buffer body)` — common path.  Resolves
  the InfoFrame type byte from `fmt.hdmiInfoFrameType()`,
  wraps body bytes, sets meta keys, and stamps the checksum.
- [x] `static HdmiInfoFrame buildRaw(uint8_t type,
  uint8_t version, Buffer body)` — escape hatch for
  InfoFrame types not in the registry.  Looks the format up
  via `fromHdmiInfoFrameType(type)`.
- [x] Accessors: `type()`, `version()`, `length()`,
  `body() const` (the bytes following the 4-byte header,
  returned as a fresh `Buffer`), `checksum()`,
  `checksumValid()`.
- [x] Implicit decay to `const AncPacket &` plus explicit
  `packet()` accessor.

**Doctests:**
- [x] AVI / Audio / SPD / DRM (HDR static) InfoFrame build
  cases with checksum-valid assertions.
- [x] `buildRaw(0x81, ...)` resolves to the
  `VendorInfoFrame` catch-all.
- [x] `buildRaw` with an unregistered type — wrapper format
  is `Invalid`, wire bytes round-trip.
- [x] `from()` rejects wrong transport / too-short data.
- [x] `body()` returns the bytes after the header
  byte-for-byte; zero-length body case handled.
- [x] Stored checksum makes the mod-256 sum of every byte
  zero (the CEA-861-G §6.1 invariant).
- [x] Implicit decay to `const AncPacket &` accepted by
  AncPacket-taking call sites.

### `AncDesc` (descriptor)

Parallel to `ImageDesc` / `AudioDesc`. Describes a single ANC
stream's shape — *not* the per-frame packets. The descriptor
is what `MediaDesc::ancList` carries and what SDP `m=`
sections round-trip.

**Files:**
- [x] `include/promeki/ancdesc.h` — declarations only as of
  the 2026-05-12 CoW conversion (DataStream operators + every
  accessor / mutator now live out-of-line in `ancdesc.cpp`).
- [x] `src/proav/ancdesc.cpp` — out-of-line implementation
  (constructors, CoW accessors, SDP `fromSdp` / `toSdp`,
  DataStream operators).
- [x] `tests/unit/ancdesc.cpp`

**Fields (Impl):**
- [x] `Size2Du32 sourceRaster` — the video raster the ANC was
  associated with (e.g. 1920×1080).  `(0,0)` if unbound.
- [x] `VideoScanMode scanMode` — interlaced/progressive,
  required for correct line interpretation.
- [x] `FrameRate frameRate` — same convention as `ImageDesc`;
  drives ST 2110-40 packet timing.
- [x] `AncFormat::IDList allowedFormats` — empty = no
  restriction; non-empty = whitelist used by sinks that only
  carry a subset.
- [x] `List<AncCategory> allowedCategories` — coarser
  filter; empty = no restriction.
- [x] `Metadata metadata` — per-stream metadata container.
- [x] `int pairedVideoStreamIndex` (default `-1`) — the
  `MediaPayload::streamIndex` of the video payload this ANC
  stream is associated with on the enclosing `Frame`.  `-1`
  means "unbound"; the ANC stream is treated as global to the
  Frame.  Producers that know the pairing (an SDI capture
  interleaving VANC with a specific link, an encoder hook
  attaching SEI captions to a specific encoded video stream,
  the TPG which stamps `0` for its single video payload) set
  this so downstream consumers can filter by pairing without
  inspecting the wire bytes.  Mirrors `pairedAudioStreamIndex`.
- [x] `int pairedAudioStreamIndex` (default `-1`) — the
  `MediaPayload::streamIndex` of the audio payload this ANC
  stream is associated with (uncommon — most ANC pairs with
  video; non-default for ANC carriages that ride alongside a
  specific audio program such as a cue point bound to one
  audio track).
- [x] Internally-CoW value-type handle (post-2026-05-07
  convention).  Wraps a private `SharedPtr<Impl>` — copy =
  refcount bump, every mutator detaches via `_d.modify()` when
  `useCount() > 1`.  No `::Ptr` alias; no `::PtrList`;
  `::List = List<AncDesc>` is a vector of value-type handles
  that share storage when copied.  Matches `Buffer`,
  `Frame`, `Metadata`, `JsonObject`, `JsonArray`, `AncPacket`.

**Methods:**
- [x] `bool isValid() const` — valid raster + valid scan
  mode, *or* an explicit `allowedFormats` /
  `allowedCategories` filter.
- [x] `bool acceptsFormat(const AncFormat &) const` —
  applies both filters; empty filters accept everything.
- [x] `bool formatEquals(const AncDesc &) const` — compares
  raster, scan mode, frame rate, both filter lists, and both
  paired stream indices; ignores metadata.  Mirror of
  `ImageDesc::formatEquals` / `AudioDesc::formatEquals`.
- [x] `int pairedVideoStreamIndex() const` /
  `setPairedVideoStreamIndex(int)`,
  `int pairedAudioStreamIndex() const` /
  `setPairedAudioStreamIndex(int)`.
- [x] SDP round-trip (RTP/ST 2110-40 only): `static AncDesc
  fromSdp(const SdpMediaDescription &)`,
  `SdpMediaDescription toSdp(uint8_t payloadType) const` using
  RFC 8331 `smpte291` rtpmap and the `DID_SDID=` fmtp grammar.
  Landed 2026-05-11 alongside `RtpPayloadAnc`.  Implementation
  hand-parses the fmtp attribute (the
  `SdpMediaDescription::fmtpParameters` map collapses duplicate
  keys, and RFC 8331 §6.2 emits one `DID_SDID=` entry per pair).
  Tolerates decimal and hex byte literals; unrecognised pairs
  round-trip as `AncFormat::Invalid` IDs on `allowedFormats`.
  `sourceRaster` / `scanMode` / `frameRate` are intentionally
  not encoded — RFC 8331 SDP does not carry them; callers
  populate from the paired `ImageDesc`.  7 new test cases /
  35 new assertions covering toSdp, fromSdp, round-trip,
  empty-filter expansion, mixed decimal/hex parsing, unknown
  DID/SDID survival, and malformed-entry tolerance.
- [x] `DataStream` operators — `TypeAncDesc = 0x5A` tag
  followed by raster / scan mode / frame rate / two filter
  lists / paired video & audio stream indices / metadata.
  Wire format extended on 2026-05-12 alongside the CoW
  conversion; the new ints ride before `metadata` so existing
  consumers below `metadata` continue to round-trip.
- [x] `operator==` / `!=` — compares every field including
  metadata, while `formatEquals` skips metadata.

### `AncPayload` (MediaPayload subclass)

The concrete `MediaPayload` that flows through the pipeline.
Inherits directly from `MediaPayload`.

**Files:**
- [x] `include/promeki/ancpayload.h`
- [x] `src/proav/ancpayload.cpp`
- [x] `tests/unit/ancpayload.cpp`

**Layout:**
- [x] Inherits `MediaPayload`, registered via
  `PROMEKI_REGISTER_MEDIAPAYLOAD` with fourcc `"ANCp"`.
  Subclass clone implemented via `_promeki_clone()`; the
  subclass's value-semantics come from `AncDesc` (CoW value
  type) and `AncPacket::List` (vector of CoW value types).
- [x] Member `AncDesc _desc`.
- [x] Member `AncPacket::List _packets`.
- [x] No backing `BufferView` — each `AncPacket` owns its own
  wire bytes via its `Buffer`.  `MediaPayload::data()` returns
  the default-constructed empty `BufferView`; serializer hooks
  write/read the packet list directly.

**Overrides:**
- [x] `kind() const → MediaPayloadKind::AncillaryData` — the
  enum value already exists in `enums.h` (value 4) alongside
  `Video` / `Audio` / `Metadata` / `Subtitle` / `Custom`;
  no enum change needed.  The type-name prefix `Anc` is the
  short form; the descriptive enum keeps its full name.
- [x] `isCompressed() const → false`.
- [x] `metadata()` forwards to `_desc.metadata()` (matches
  the VideoPayload pattern).
- [x] `hasDuration() const → true`; `duration()` returns the
  stamped frame duration.
- [x] `subclassFourCC() const → 'ANCp'`.
- [x] `_promeki_clone() const` — deep clone (packet list copy
  bumps refcounts on each packet's Impl).
- [x] `serialisePayload(DataStream &)` writes desc + duration
  + packet count + each packet.
- [x] `deserialisePayload(DataStream &)` inverse.
- [x] `variantLookup*` via
  `PROMEKI_MEDIAPAYLOAD_LOOKUP_DISPATCH`; scalar keys
  registered: `PacketCount`, `HasCaptions`, `HasTimecode`,
  `HasAfd`, `HasHdr`, `HasSplice`.

**Methods:**
- [x] `const AncDesc &desc() const`, `AncDesc &desc()`,
  `void setDesc(const AncDesc &)`.
- [x] `const AncPacket::List &packets() const`,
  `AncPacket::List &packets()`,
  `void addPacket(const AncPacket &)`,
  `void clearPackets()`.
- [x] `AncPacket::List packetsOfFormat(const AncFormat &) const`.
- [x] `AncPacket::List packetsOfCategory(const AncCategory &) const`.
- [x] `AncPacket::List packetsOfTransport(const AncTransport &) const`.
- [x] `bool hasFormat(const AncFormat &) const`,
  `bool hasCategory(const AncCategory &) const`.
- [x] `bool isExclusiveExtras() const` — `true` by default
  (ANC packets' `Buffer`s are typically freshly built per
  capture and don't alias other payloads' base data).

### `MediaDesc` + `Frame` plumbing

Wire `AncDesc` into `MediaDesc`; add an ANC convenience
accessor to `Frame`.

**Files:**
- [x] `include/promeki/mediadesc.h` — adds `AncDesc::List
  _ancList` member, `ancList()` const + mutable accessors,
  updates `isValid` to accept ANC-only descriptors (a
  metadata-only stream is meaningful, e.g. an ST 2110-40
  receiver with no paired -20 video), updates `formatEquals`,
  updates equality, updates DataStream `<<` / `>>`.
- [ ] `MediaDesc::fromSdp` / `toSdp` ANC `m=` section
  round-trip — **deferred to Phase 1** alongside
  `AncDesc::fromSdp` / `toSdp` and `RtpMediaIO::buildSdp` /
  `applySdp`.
- [x] `include/promeki/frame.h` + `src/proav/frame.cpp` —
  adds `AncPayload::PtrList ancPayloads() const` mirroring
  `videoPayloads` / `audioPayloads`.  `mediaDesc()` populates
  the new `ancList()` from every `AncPayload` on the frame.
- [ ] `tests/unit/mediadesc.cpp` / `tests/unit/frame.cpp`
  updates for the new surface — **not required for Phase 0**.
  The existing mediadesc/frame tests still pass under the
  expanded shape (the ANC list defaults to empty so every
  existing assertion is unchanged); a dedicated round-trip
  test through `MediaDesc` / `Frame` lands when Phase 1 ships
  the first real ANC producer that needs the integration
  exercised end to end.

**Out of scope here:**
- No pipeline-planner consequences — ANC streams ride along
  the same `MediaIOPortGroup` as the producing backend until
  a future use case demands a separate route.

---

## Phase 1 — RTP ST 2110-40 / RFC 8331 wire format

ST 2110-40 / RFC 8331 packetization. This is the first wire
format the ANC stack supports end-to-end and the one the rest
of the plan validates against. The 2110-40 wire works
**directly on `AncTransport::St291` packets** — no translation
on ingress or emit. Pure pack/unpack against the canonical
ST 291 storage form.

### `RtpPayloadAnc` (RtpPayload subclass)

Implements RFC 8331 pack/unpack for `AncPayload`. Lives next
to the other `RtpPayload*` classes.

**Status:** Landed 2026-05-11.  5 test cases / 155 assertions
covering round-trip, multi-packet, field-B/progressive, every
DataCount modulo 1..8, MTU splitting, validate gating, and
truncation rejection.  Two scope notes captured inline below
(both deferred deliberately):

- **ESN handling deferred to Phase 1.5.** The packer writes
  `ESN=0` and the depacketizer ignores ESN.  Correct ESN
  handling requires plumbing a 32-bit logical sequence through
  the reorder buffer (which today only sees 16-bit RTP seq);
  that's orthogonal to wire-format correctness and lands
  alongside any cross-stream sequence-aware reorder work.
- **F-bit is two-state in practice.** Packer emits F=00 (`Progressive`)
  or F=11 (`InterlacedField2`) only — F=10 (`InterlacedField1`)
  would require a separate "is interlaced" hint per packet that
  `AncMeta::St291::FieldB` (bool) does not carry.  Once
  `AncDesc::scanMode` is plumbed through the packetizer thread
  (Phase 1, RtpMediaIO ANC stream wiring) the packer can promote
  `FieldB=false + scanMode=Interlaced` to F=10.  Until then F=00
  also covers the field-1 case — sinks that key off field parity
  must consult `AncDesc::scanMode`.

**Files:**
- [x] `include/promeki/rtppayloadanc.h`
- [x] `src/network/rtppayloadanc.cpp`
- [x] `tests/unit/network/rtppayloadanc.cpp`

**Interface:**
- [x] Derives `RtpPayload`. `payloadType()` defaults to 100
  (dynamic, configurable); `clockRate()` = 90 000 (RFC 8331
  §3.2).
- [x] Typed pack overload: `RtpPacket::List packAncFrame(
  const AncPacket::List &packets, uint32_t rtpTimestamp)`.
  Non-`St291` packets in `packets` are silently skipped (the
  packer keeps going so a single stray non-`St291` entry from
  an upstream stage does not take the wire down).  Each
  packet's wire-form `data()` is copied verbatim with the
  surrounding ANC_Count + line/h-offset/F/StreamNum fields
  read from `packet.meta()`.  RTP timestamp is stamped on
  every produced packet; the RTP marker bit is set on the
  last packet of the frame.  Sequence number, SSRC, and
  payload-type live elsewhere — the TX thread stamps them,
  matching the convention every other `RtpPayload` subclass
  uses (`pack` deliberately leaves them at zero so the only
  owner of those fields is the TX path).
- [x] Typed unpack: `Error unpackAncPackets(const
  RtpPacket::List &in, AncPacket::List &out)` — produces
  `AncTransport::St291` packets with `data()` set to the
  raw RFC 8331 per-packet bytes and meta keys populated
  from the surrounding ANC fields.  Zero translation; preserves
  wire bytes for byte-exact replay.  Returns `Error::OutOfRange`
  with a logged `promekiWarn` on truncation (the `Error` class
  is int-only by convention — error strings go through
  `promekiWarn` or an `errorString()` accessor when the API
  needs to surface them).
- [x] The byte-stream `pack(const void *, size_t)` and
  `unpack(const RtpPacket::List &)` from `RtpPayload` return
  empty containers and emit a `promekiWarn` (rather than
  asserting unreachable, so a bug elsewhere doesn't take the
  process down).
- [x] `validate(Buffer)` returns `DropSilently` for buffers
  smaller than the 8-byte payload header, zero `ANC_Count`
  payloads, or payloads whose declared `Length` overruns the
  buffer; otherwise `Accept`.
- [x] `maxPayloadSize()` honors the standard MTU-safe default
  (1200 bytes inherited from `RtpPayload`).

**Wire format (per RFC 8331 §2.1):**
- [x] Payload header: 16-bit Extended Sequence Number (high
  bits of the 32-bit logical sequence number; low bits are
  the RTP header's 16-bit Sequence Number) + 16-bit Length
  + 8-bit ANC_Count + 2-bit F + 22 reserved bits.
- [x] Per ANC packet: 4-byte per-packet header
  (C-bit / 11-bit Line_Number / 12-bit Horizontal_Offset /
  S-bit / 7-bit StreamNum) + the packet's stored `data()`
  bytes (DID / SDID / DataCount / UDW / CS, 10-bit packed) +
  zero-pad to the next 32-bit boundary.
- [x] Honor the marker bit on the last RTP packet of a frame.
- [ ] ESN handling: the depacketizer threads the 16-bit ESN
  together with the RTP sequence number into a 32-bit logical
  sequence for reorder / loss detection (same shape as the
  ST 2110-20 video payload).  **Deferred to Phase 1.5** —
  packer writes ESN=0, unpacker ignores ESN.  Requires either
  a 32-bit-aware variant of the existing reorder buffer or a
  per-RX wrap-detection counter; orthogonal to wire-format
  correctness.

**10-bit pack/unpack helpers — pending consolidation.**
`st291packet.cpp` already has a full bit-level pack/unpack pair
in its anonymous namespace; `rtppayloadanc.cpp` has a small
`readWord10` for the three header words it reads directly.  Once
the typed parsers in Phase 2 land (each will need to walk 10-bit
ST 291 streams) the pack/unpack should consolidate into a
shared header — `include/promeki/st291bits.h` is the natural
name.  Tracked as a Phase 2 cleanup task; not blocking RTP-40.

**Buffer const-correctness — landed 2026-05-11.**  As a sidecar
to this phase: `Buffer::data` / `Buffer::odata`,
`BufferView::data`, and `BufferView::Entry::data` now have dual
const/non-const overloads (const handle → `const void *` /
`const uint8_t *`; non-const handle → mutable view).  The
"mutate through const handle" idiom is preserved on the mutators
(`copyFrom`, `setSize`, `fill`) for backwards compatibility but
removed from the read accessors.  Audit touched
`csccontext.cpp` (drop spurious `const` on `buffer`),
`nvdecvideodecoder.cpp` and `ndimediaio.cpp` (const_cast at
external-API boundaries — cudaMemcpy and NDI's non-const
`p_data`), and two tests.  All 6060 test cases pass.

**Doctests:**
- [x] Round-trip every well-known ST 291 format through
  pack/unpack — byte-exact, including stored (not recomputed)
  checksums.  Covered by the per-format build + round-trip test
  case (`Cea708 / Afd / AtcLtc / AtcVitc2`) plus the per-`DC`
  modulo sweep.
- [x] Multi-packet ANC frames where the wire layout splits at
  byte boundaries between ANC packets.
- [x] Field-B / progressive line numbering.
- [x] Word-alignment padding at every UDW count modulo (DC=1..8).
- [x] Truncated / under-length input handling (`Error::OutOfRange`
  with a logged warning) and `validate` gating short / zero-count
  buffers as `DropSilently`.

### RtpMediaIO ANC stream wiring

**Status:** Landed 2026-05-11.  Decision on the
"one m=section or two?" open question is captured below: we went
with **one** — the existing data m=section becomes the ANC
section when `DataRtpFormat::St2110_40` is selected.  This keeps
the change footprint small and matches the devplan's
Open-Questions recommendation; if a deployment ever needs to
carry JSON metadata *and* ANC simultaneously, a second
m=application section can be added incrementally without
breaking the current wiring.

**MediaConfig keys — reused, not duplicated:**
- [x] `DataRtpDestination`, `DataRtpPayloadType`,
  `DataRtpClockRate`, `DataRtpSsrc`, `DataRtpDscp` cover ANC
  too — no new `AncRtp*` keys were added.  `DataRtpFormat`
  selects the wire shape: `JsonMetadata` keeps the legacy
  JSON path; `St2110_40` flips to RFC 8331 ANC.  When ANC is
  selected the clock rate is force-set to 90000 Hz (with a
  warning if the config disagreed) per RFC 8331 §3.2.

**Per-stream worker threads:**
- [x] `RtpAncPacketizerThread` (header + cpp + 3 unit tests).
  Pulls `AncPayload` off the inbound `RtpFrameWork`, calls
  `RtpPayloadAnc::packAncFrame`, pushes an `RtpPacketBatch`
  with `clockRate = 90000` and `markerOnLast = true` onto the
  shared `DataTxThread` packet queue.  Non-St291 packets in
  the input list are silently skipped (the packetizer is
  best-effort; the upstream `AncTranslator` is expected to
  translate them).  Empty AncPayloads produce no batch — a
  legitimate quiescent state, not an error.
- [x] `RtpAncDepacketizerThread` (header + cpp + 4 unit
  tests).  Accumulates RTP packets across the marker-bit /
  timestamp boundary, runs `RtpPayloadAnc::unpackAncPackets`,
  stamps a `captureTime` (SR-derived when available, anchor-
  interpolated otherwise), and pushes one `RxAncFrame` per
  ANC frame onto the per-stream queue.
- [x] `RxAncFrame` lives in `rxpayloadbundle.h` alongside
  `RxDataMessage` / `RxAudioChunk` / `RxVideoFrame`.  Carries
  the per-stream `AncDesc`, the `AncPacket::List`, RTP TS,
  captureTime, wallclock NTP, and first-packet arrival
  timestamp.

**`RtpMediaIO` plumbing:**
- [x] `configureDataStream` branches on `DataRtpFormat`.  The
  St2110_40 path creates an `RtpPayloadAnc`, sets the rtpmap
  to `smpte291/90000`, and pre-stamps the `fmtp` string with
  `DID_SDID=…` entries from `AncDesc().toSdp(payloadType)`
  (full St291 registry by default).
- [x] `openStream` (writer) dynamic-casts `s.payload` — when
  it's an `RtpPayloadAnc`, the strand spawns
  `RtpAncPacketizerThread` instead of the nested
  `DataPacketizerThread`.  `DataTxThread` is shared
  unchanged (it's payload-agnostic — pops `RtpPacketBatch`,
  stamps RTP TS via cumulativeTicks, sends).
- [x] `openReaderStream` (reader) does the same dispatch —
  ANC payloads instantiate `RtpAncDepacketizerThread` against
  a fresh `Queue<RxAncFrame>` on the `DataReaderStream`'s
  new `ancPayloadQueue` field; legacy JSON falls through to
  `RtpDataDepacketizerThread`.

**RtpAggregatorThread integration:**
- [x] Aggregator context gains `RtpAggregatorAncStream anc` (parallel
  to the existing `video / audio / data` views) plus a new
  `Mode::AncOnly` for ANC-only receivers.
- [x] In `Mode::Video`, `drainAncBefore(windowEnd, out)` mirrors
  `drainDataBefore` — pulls ANC frames whose captureTime falls
  inside the current video window into the outgoing Frame.
  Frames popped past the window are parked in `_pendingAnc`
  (same put-back pattern as the data path).  Watchdog
  continuation Frames also pick up any pending ANC.
- [x] `Mode::AncOnly` emits one Frame per `RxAncFrame` with an
  `AncPayload` populated from `anc.desc` + `anc.packets`.

**SDP round-trip:**
- [x] `RtpMediaIO::buildSdp` automatically picks up the ANC
  rtpmap (`smpte291/90000`) and fmtp (`DID_SDID={…}`) from
  the Stream's `rtpmap` / `fmtp` fields — no ANC-specific
  branching in `buildSdp` itself.  `AncDesc::toSdp` (Phase 0
  deferred item, landed earlier today) produces the fmtp
  string.
- [x] `applySdp` recognises the `smpte291` rtpmap encoding on
  an application m=section and sets
  `DataRtpFormat = St2110_40` so `configureDataStream` picks
  the ANC path on open.  Per-stream `AncDesc::fromSdp` parsing
  of the fmtp DID_SDID list to populate
  `DataReaderStream::desc.allowedFormats` is deferred to a
  follow-up — the depacketizer currently ships a default-
  constructed `AncDesc` on every emitted `RxAncFrame`.

**Doctests / integration:**
- [x] `tests/unit/network/rtpancpacketizerthread.cpp` — 3
  cases: one-batch packing, empty payload, missing-streamIdx.
- [x] `tests/unit/network/rtpancdepacketizerthread.cpp` — 4
  cases: marker-bit emit, timestamp-change flush, SSRC reset,
  inactive gate.
- [ ] `tests/func/anc-rtp40-roundtrip/` — TPG → RTP-40 →
  receiver → byte-exact ANC compare.  Deferred to Phase 6
  alongside the rest of the functional matrix (`TpgMediaIO`
  needs the ANC-emission mode the devplan calls for in
  Phase 6 before this test can run end-to-end).

**Deferred / follow-ups captured during integration:**
- ANC `AncDesc::fromSdp` is not yet plumbed into the reader's
  per-stream descriptor — the depacketizer stamps a default
  AncDesc on each `RxAncFrame`.  The aggregator forwards
  whatever it gets, so the path is correct but loses
  allowedFormats / raster context.  Wire when the first
  consumer needs it (likely the ANC inspector in Phase 6).
- ESN handling stays at the wire-format level (always 0).
  See Phase 1.5 note under `RtpPayloadAnc`.
- The ANC packetizer reuses `DataTxThread` rather than
  spawning its own TX thread.  Works because the TX thread
  is payload-agnostic and the rate cap doesn't apply to ANC
  (which is sparse).  If a future deployment needs distinct
  pacing for ANC vs. data, a dedicated `RtpAncTxThread` is a
  drop-in replacement at the strand assembly point.

---

## Phase 2 — `AncTranslator` + handler registries

**Framework status:** Landed 2026-05-11.  `AncTranslateConfig` +
`AncTranslator` + the three registries (parsers, builders, direct
translators) all build clean and pass tests; ATC (LTC / VITC1 /
VITC2) and AFD ← → St291 codecs ship as the initial validation
set (38 new test cases, 295 new assertions).  Remaining codecs
(CEA-708, SCTE-104, HDR, KLV, NDI XML, RTMP AMF, HLS SEI) are
deferred to a Phase 2b follow-up — the framework contract is
stable so each can land independently.

**Decisions made during build (deviations from the original sketch):**

- **`AncTranslateConfig` is header-only.**  Mirrors the existing
  `MediaConfig` precedent (also header-only).  No `.cpp` lands —
  the small JSON `toString` / `fromString` helpers stay inline.
- **`TypeAncTranslateConfig` Variant integration deferred.**  Same
  rationale as `TypeAncPacket` in Phase 0: the config inherits
  `VariantDatabase<"AncTranslateConfig">`, which transitively
  depends on `Variant` itself, so adding it to the X-macro creates
  a circular include.  Not load-bearing for Phase 2 — the config
  rides through DataStream via the template `VariantDatabase`
  operators and through JSON strings via `toString` /
  `fromString`, neither of which needs Variant integration.
- **`MediaConfig::AncTranslateConfig` key deferred to Phase 4.**
  No backend consumes it yet (every parse / build / translate
  call already gets the held config from the `AncTranslator`
  instance).  Easy to add when the first MediaIO backend (NDI,
  RTMP) plumbs it.
- **Typed-key naming flattened.**  The devplan's
  `St291::BuildLine` form is not representable as a single C++
  identifier in a flat `VariantDatabase`-style key registry.  The
  C++ identifier collapses the `::` to `St291BuildLine`; the
  wire-name keeps the dot for readable JSON output
  (`"St291.BuildLine"`).  Functionally identical.
- **Combined parser+builder per (format, transport) file.**  The
  original devplan wanted one file per direction
  (`ancparser_<F>_<T>.cpp` and `ancbuilder_<F>_<T>.cpp`).  In
  practice parser and builder for the same wire layout share so
  much encoding logic that splitting them doubles file count for
  zero benefit.  Single file per format-pair
  (`anccodec_<format>.cpp`) lives in `src/proav/` and holds the
  registration for every transport the format speaks on.
- **Three ATC IDs share one parser.**  AtcLtc / AtcVitc1 / AtcVitc2
  use identical SMPTE 12M-2 wire layout — the only differentiator
  is the SDID byte, which the registry already keys on.  The same
  `parseAtcSt291Impl` is registered against all three IDs;
  the builder side has three thin format-specific entry points so
  the SDID stamps correctly on emit.

The rest of this section documents the original plan; check-boxes
reflect what landed vs. what was deferred.

One stateful class — `AncTranslator` —
backed by three independent registries keyed on `AncFormat::ID`:

- **Parsers:** `(format, src_transport) → fn(AncPacket,
  cfg) → Result<Variant>`. Decodes wire bytes to a typed
  Variant (`Cea708Cdp`, `Timecode`, `Scte104Message`, …).
- **Builders:** `(format, dst_transport) → fn(Variant,
  cfg) → Result<AncPacket>`. Encodes a typed Variant into a
  packet on the requested transport.
- **Translators (direct, optional):** `(format,
  src_transport, dst_transport) → fn(AncPacket, cfg) →
  Result<AncPacket>`. Optimization for hot wire-to-wire
  paths; when not registered, `AncTranslator::translate`
  falls back to `parse + build` automatically.

`AncTranslator` is a value-type session class that holds an
`AncTranslateConfig` and dispatches to the registered handlers.
Construct once per stream / per sink-emit loop and reuse.

### `AncTranslateConfig` (config object)

Mirrors the MediaConfig pattern: a thin wrapper around a
`Metadata` member, with typed key constants for the well-known
knobs. Passed by `const &` to every `translate`, `decode`,
`encode`, and typed-parser parse/build call. Variant-registered
so the whole config can ride through MediaConfig, DataStream,
and CLI tooling as one bundled value.

**Files:**
- [x] `include/promeki/anctranslateconfig.h` — header-only
  (mirrors the @ref MediaConfig precedent).
- (No `.cpp` — the small JSON helpers stay inline.)
- [x] `tests/unit/anctranslateconfig.cpp` (path `tests/unit/`
  rather than the original `tests/unit/proav/` per the flat
  test-tree convention established in Phase 0).

**Prerequisite — `Metadata` JSON string round-trip.**  Landed
during Phase 0; `AncTranslateConfig::toString` / `fromString`
delegate to the same `JsonObject::parse` / `toString` path the
base `VariantDatabase` already wires up.

**Class surface:**
- [x] Default constructor.
- [x] Templated `get` / `set` / `has` / `getAs` inherited from
  `VariantDatabase` — no class-specific overloads needed.
- [x] `merge` (inherited from `VariantDatabase`).
- [x] `String toString(unsigned int indent = 0) const`.
- [x] `static AncTranslateConfig fromString(const String &,
  Error *err = nullptr)`.
- [x] `operator==` / `!=` (inherited).
- [x] DataStream operators (inherited template).
- (No `raw()` accessor — `VariantDatabase`'s inherited
  iteration surface (`forEach`, `ids`, …) covers the same
  ground without leaking a `Metadata` reference.)
- (Variant integration `TypeAncTranslateConfig` deferred for
  the same circular-include reason `TypeAncPacket` was
  deferred in Phase 0.)

**Well-known typed keys (static constexpr members):**
- [x] `AncTranslateConfig::Fidelity` — `AncFidelity` (default
  `Default`).
- [x] `AncTranslateConfig::Checksum` — `AncChecksumPolicy`
  (default `PreserveOrRecompute`).
- [x] `AncTranslateConfig::OnUnsupported` — `AncOnUnsupported`
  (default `BestEffort`).
- [x] `AncTranslateConfig::AllowLossy` — `bool` (default
  `false`).

**Per-transport build-time keys.**  Distinct from
`AncMeta::<Transport>::…` — those are *read-time* sidecar
keys on a captured packet (e.g. `AncMeta::St291::Line` =
"this packet was captured on VANC line 11"). The
`AncTranslateConfig` keys here are *build-time* inputs to
codecs that produce packets on that transport from a typed
Variant (e.g. `AncTranslateConfig::St291BuildLine` =
"when you synthesise an ST 291 packet, place it on line
11"). The codec writes the build-time input into the
packet's `AncMeta` sidecar.  C++ identifier collapses the
namespacing dot (`St291BuildLine`); wire-name keeps the dot
(`St291.BuildLine`) for readable JSON output.

- [x] `AncTranslateConfig::St291BuildLine` (`uint16_t`).
- [x] `AncTranslateConfig::St291FieldB` (`bool`).
- [x] `AncTranslateConfig::NdiXmlNamespace` (`String`).
- [x] `AncTranslateConfig::HdmiInfoFrameOui` (`uint32_t`).
- [x] `AncTranslateConfig::RtmpAmfObjectName` (`String`).

**Doctests (12 cases / 45 assertions, all passing):**
- [x] Default-construction returns documented defaults for
  every well-known key (via spec lookup).
- [x] `set` then `get` round-trip for universal knobs.
- [x] `set` then `get` round-trip for per-transport keys.
- [x] `getAs` falls back to spec default when missing.
- [x] `merge` overlay semantics (later wins, untouched keys
  preserved).
- [x] DataStream round-trip.
- [x] `toString` produces non-empty JSON containing each set
  key.
- [x] `fromString(toString())` round-trips for a representative
  config covering enum, bool, integer, and string-valued keys.
- [x] Indented `toString(2)` is also round-trippable.
- [x] `fromString` on malformed JSON sets `err` and returns
  empty.
- [x] Empty config toString / fromString round-trip.
- [x] Equality compares stored entries.
- (No standalone Variant round-trip test — Variant integration
  is deferred.)

**MediaConfig pipeline-level default.** One bundled key
carrying the whole config:
- [ ] `MediaConfig::AncTranslateConfig`
  (`AncTranslateConfig`) — set on a MediaIO to use this
  config for every translate / codec call on that backend.
  Per-call overrides are still possible by constructing a
  fresh config from `MediaConfig::AncTranslateConfig` and
  `merge`-ing per-format adjustments on top.

### `AncTranslator` class + handler registries

**Files:**
- [ ] `include/promeki/anctranslator.h`
- [ ] `src/proav/anctranslator.cpp`
- [ ] `tests/unit/proav/anctranslator.cpp`

**Files:**
- [x] `include/promeki/anctranslator.h`
- [x] `src/proav/anctranslator.cpp`
- [x] `tests/unit/anctranslator.cpp` (flat test tree)

**Class surface:**
- [x] `AncTranslator()` — empty config (defaults apply).
- [x] `explicit AncTranslator(AncTranslateConfig cfg)`.
- [x] `const AncTranslateConfig &config() const`,
  `void setConfig(AncTranslateConfig)`.
- [x] `Result<Variant> parse(const AncPacket &) const`.
- [x] `Result<AncPacket> build(const Variant &, const AncFormat &,
  const AncTransport &) const`.
- [x] `Result<AncPacket> translate(const AncPacket &,
  const AncTransport &) const` — identity short-circuit on
  `src == dst`, then direct translator, then composed
  parse+build.
- [x] Static capability queries (`hasParser`, `hasBuilder`,
  `hasDirectTranslator`, `canTranslate`).
- [x] Capability enumeration (`registeredParserTransports`,
  `registeredBuilderTransports`).

(The signatures take `const AncFormat &` / `const AncTransport &`
rather than by-value as the original devplan sketched — matches
how every other ANC API already passes these handle types.)

**Registration surface:**
- [x] `using ParserFn = Result<Variant>(*)(const AncPacket &,
  const AncTranslateConfig &)`.
- [x] `using BuilderFn = Result<AncPacket>(*)(const Variant &,
  const AncTranslateConfig &)`.
- [x] `using TranslatorFn = Result<AncPacket>(*)(
  const AncPacket &, AncTransport, const AncTranslateConfig &)`
  (third arg is the target transport — the original devplan
  signature missed this).
- [x] `static void registerParser(AncFormat::ID,
  const AncTransport &, ParserFn)`.
- [x] `static void registerBuilder(AncFormat::ID,
  const AncTransport &, BuilderFn)`.
- [x] `static void registerTranslator(AncFormat::ID,
  const AncTransport &, const AncTransport &, TranslatorFn)`.
- [x] Macros: `PROMEKI_REGISTER_ANC_PARSER(Tag, Format,
  Transport, Fn)`, `PROMEKI_REGISTER_ANC_BUILDER(...)`,
  `PROMEKI_REGISTER_ANC_TRANSLATOR(...)`.  The leading @c Tag
  argument lets the same `.cpp` register the same handler under
  several format IDs without C++-identifier collisions (the ATC
  codec uses this — one parser, three format-specific
  registrations).

**Storage / thread-safety:**
- [x] Three `Map`s with small composite-key structs
  (`{format, src}`, `{format, dst}`, `{format, src, dst}`) plus
  a single process-wide `Mutex` covering all three.  Tighter
  locking is not justified given the registries are populated at
  static init and read-mostly thereafter.  Re-registration logs
  a warning (matches `MediaPayload`'s registry behaviour) but
  proceeds — tests need to be able to re-register against the
  same TestFormat across runs of `--rerun`.

**Behaviour governance via `cfg`:**
- The held `AncTranslateConfig` is threaded into every parser /
  builder / direct-translator call.  The shipped ATC and AFD
  codecs honour `St291BuildLine` and `St291FieldB`; the rest of
  the universal knobs land as new codecs need them.

**Doctests (13 cases / 38 assertions, all passing):**
- [x] `parse` dispatch via stub-registered parser.
- [x] `parse` propagates held config to the handler.
- [x] `parse` returns `Error::NotSupported` when none registered.
- [x] `build` dispatch via stub-registered builder.
- [x] `build` returns `Error::NotSupported` when none registered.
- [x] `translate` identity short-circuit returns packet
  unchanged.
- [x] `translate` falls back to composed parse+build chain.
- [x] `translate` prefers direct translator when registered.
- [x] `translate` returns `Error::NotSupported` when no path
  exists.
- [x] `canTranslate` reports composed and direct paths.
- [x] `registeredParserTransports` /
  `registeredBuilderTransports` enumerate entries.
- [x] `hasParser` / `hasBuilder` reflect registrations.
- [x] `config()` / `setConfig` getters/setters.
- (No double-registration rejection test — current behaviour is
  "log warning and replace," which suits the test harness's
  re-registration model.)

### Initial parser / builder / translator set

**Initial Phase 2 codecs landed (2026-05-11):**

- [x] `AtcLtc ← St291`, `AtcVitc1 ← St291`, `AtcVitc2 ← St291`
  → `Variant(Timecode)`.  Shared `parseAtcSt291Impl` registered
  under all three IDs.
- [x] `AtcLtc → St291`, `AtcVitc1 → St291`, `AtcVitc2 → St291`
  built from `Variant(Timecode)`.  Three thin format-specific
  build entry points so the SDID stamps correctly on emit.
  Honour `AncTranslateConfig::St291BuildLine` and
  `St291FieldB`.  Binary groups and BGF / CF / polarity bits are
  emitted as zero (and ignored on parse).
- [x] `Afd ← St291` → `Variant(uint8_t)` (AFD code in bits 3..6,
  AR flag in bit 7).
- [x] `Afd → St291` built from `Variant(uint8_t)`.  Bar data
  slots emit as zero; downstream layers can extend the codec
  when they need to carry bar values.

**File layout** (deviation from the original devplan):
- One `.cpp` per format-pair under `src/proav/anccodec_<format>.cpp`,
  not separate `ancparser_*` / `ancbuilder_*` files — parser and
  builder for the same wire layout share too much encoding logic
  to split.
- Per-codec doctest at `tests/unit/anccodec_<format>.cpp` (flat
  test tree).

**Remaining codecs — Phase 2b follow-up** (in priority order):

- [x] `Cea708 ← / → St291` — landed 2026-05-11 alongside the
  Phase 3 `Cea708Cdp` typed value type, TPG caption injection,
  and Inspector AncData JSONL dump (see Phase 3 section below).
- [ ] `Cea608 ← / → St291`.
- [ ] `Scte104 ← / → St291` (`Scte104Message` value type from
  Phase 3).
- [ ] `HdrStatic2086 ← / → St291`,
  `HdrStatic2086 ← / → HdmiInfoFrame` (DRM InfoFrame).
- [ ] `HdrDynamic2094_40 ← / → St291`,
  `HdrDynamic2094_40 ← / → HdmiInfoFrame`.
- [ ] `Afd ← NdiXml`, `Afd → NdiXml`, `Afd ← HdmiInfoFrame`
  (AVI InfoFrame aspect bits), `Afd → HdmiInfoFrame`.
- [ ] `Cea708 ← NdiXml`, `Cea708 → NdiXml`.
- [ ] `Cea708 ← RtmpAmf`, `Cea708 → RtmpAmf` — AMF0
  `onCaptionInfo`.
- [ ] `Cea708 ← HlsSei`, `Cea708 → HlsSei`.
- [ ] `Scte35 ← MpegTsPrivate`, `Scte35 → MpegTsPrivate`.

**Direct translators (only the ones with real efficiency or
fidelity reasons to bypass parse+build):**
- [ ] None in Phase 2's initial set — the composed parse+build
  path is correct for every pair and adequate at expected
  packet rates. Add direct translators only when profiling
  shows a hot path that needs them.

**Naming convention:**
- File: `src/proav/ancparser_<Format>_<Transport>.cpp`,
  `src/proav/ancbuilder_<Format>_<Transport>.cpp`.
- Free functions: `parse<Format><Transport>` /
  `build<Format><Transport>` registered into the
  `AncTranslator` registries at static-init via the macros.

---

## Phase 3 — Typed parsers and builders

Application-facing typed views. Each parser produces a typed
Variant (`Cea708Cdp`, `Timecode`, etc.) and consumes one to
build an `AncPacket`. They are thin wrappers over
`AncTranslator::parse` / `AncTranslator::build` and work
against any transport that has a registered parser/builder,
not just St291.

### CEA-708 / CEA-608 captions

**Status:** Landed 2026-05-11 (Phase 2b CEA-708 end-to-end slice).
21 unit tests / 86 assertions on the value type + codec, plus
the per-frame TPG injection tests and full-pipeline Inspector
AncData test (rewritten and expanded in Phase 3.5c to feed a
SubRip file through `Cea608Encoder` instead of a static string —
10 TPG-injection cases + 1 Inspector AncData case, both passing).
End-to-end demo through `mediaplay` confirmed that TPG-emitted
CEA-708 packets land in the Inspector JSONL with the original
caption text recoverable from the cc_data triples (parity-bit
strip).

**Files:**
- [x] `include/promeki/cea708cdp.h` + `src/proav/cea708cdp.cpp`
  — value type + wire encode/decode + JSON dump + DataStream
  operators.  Variant payload type `TypeCea708Cdp` (tag 0x5B);
  added to `variant.h` X-macro + the datastream `has_free_*`
  trait allowlist + `readVariantPayload` switch.
- [x] `src/proav/anccodec_cea708.cpp` — `Cea708 ← St291` parser
  and `Cea708 → St291` builder registered through the Phase 2
  framework.  Honours `AncTranslateConfig::St291BuildLine` /
  `St291FieldB`.  Caps CDP size at 255 bytes (ST 291 DataCount
  is one byte) — over-cap callers get `Error::OutOfRange`.
- [x] `tests/unit/cea708cdp.cpp` — round-trip, checksum,
  identifier, length, footer-sequence-mismatch, JSON, Variant,
  DataStream, opaque-extra-bytes preservation.
- [x] `tests/unit/anccodec_cea708.cpp` — capability queries +
  full round-trip via `AncTranslator` + cfg-threaded line/field
  + truncation rejection + sequence-counter mirror.

**Cea708Cdp value type:**
- [x] CDP header (identifier 0x9669, length, frame-rate code,
  flags, sequence counter).
- [x] Optional time code section (id 0x71) — round-trips through
  the existing `Timecode` value type.
- [x] cc_data section (id 0x72) carrying a `List<CcData>` of
  `{valid, type, b1, b2}` triples (cc_type 0–3).
- [x] Opaque `extraBytes` pass-through for ccsvcinfo (id 0x73)
  and any future sections — captured packets round-trip
  byte-exact even when the library doesn't model the section.
- [x] Footer (id 0x74) with mirrored sequence counter and
  packet_checksum stamping (mod-256 byte sum zero).
- [x] `toBuffer` / `fromBuffer` for SMPTE 334-2 wire-form
  encode / decode.
- [x] `toJson()` for structured inspection output.
- [x] `toString()` for log diagnostics.
- (No standalone `Cea708Cdp::parse` / `Cea708Cdp::build`
  helpers — application code uses `AncTranslator::parse` /
  `AncTranslator::build` directly with a `Variant(Cea708Cdp)`.
  Adding the standalone helpers later is a thin wrapper if a
  consumer demands them.)
- (No CEA-608-only legacy helper.  cc_type=0/1 triples surface
  naturally on the `CcDataList`; layering a CEA-608 styled
  parser on top is a future task with its own value type.)

**TPG CEA-708 injection (the "realish use case" half):**

The initial Phase 2b TPG injector emitted a single fixed string as
raw byte-pairs every frame.  Phase 3.5 replaced that with a real
SubRip-driven flow (see the Phase 3.5c entry below); the configuration
surface evolved accordingly.  Current state:

- [x] `MediaConfig` keys: `TpgAncCaptionsEnabled` (bool),
  `TpgAncCaptionsFile` (String — path to a SubRip `.srt` file,
  Phase 3.5), `TpgAncCaptionsOffset` (Duration — added to every
  cue's start/end before scheduling, Phase 3.5),
  `TpgAncCaptionsLine` (int — VANC line, default 11).  Disabled
  by default.
- [x] When enabled, `TpgMediaIO::executeCmd(Read)` pulls the
  per-frame `CcDataList` from the held `Cea608Encoder`, wraps
  it into a `Cea708Cdp`, runs `AncTranslator::build`, and
  attaches a fresh `AncPayload` to the Frame.  Sequence counter
  advances per frame; timecode section mirrors
  `Metadata::Timecode` when timecode is enabled.  The encoder's
  state machine produces real CEA-608 control codes (RCL → PAC
  → chars → EOC → EDM) timed to each cue's display window —
  the byte stream looks like a real broadcast captioner's
  output, not a static repeat.
- [x] Per-cue `Metadata::Subtitle` stamp on each cue's start
  frame so downstream consumers can read the active cue without
  decoding ANC.
- [x] Frame-rate code is derived from the configured `FrameRate`
  (1=23.976, 2=24, 3=25, 4=29.97, 5=30, 6=50, 7=59.94, 8=60).
- [x] `mediaDesc().ancList()` advertises the CEA-708 stream so
  the planner / SDP path sees what the TPG produces.
- [x] `tests/unit/tpg_anc_captions.cpp` — 10 cases covering:
  disabled-emits-nothing, enabled-no-file-emits-null-pairs,
  file-driven-pop-on-byte-stream-at-expected-frames,
  offset-shifts-cues, malformed-file-fails-ParseFailed,
  missing-file-fails, Metadata::Subtitle-stamped-on-start-frame,
  sequence-counter-advances, MediaDesc-advertises-CEA708,
  VANC-line-carry-through.

**Inspector AncData JSONL dump (the other half):**
- [x] New `InspectorTest::AncData` (value 8) enum entry +
  `MediaConfig::InspectorAncDataFile` (String).  Empty file
  path = auto-generate `promeki_inspector_anc_<pid>_<ns>.jsonl`
  under `Dir::temp()`.
- [x] `InspectorMediaIO::runAncDataCheck` walks
  `frame.ancPayloads()`, parses each `AncPacket` via the
  internally-held `AncTranslator`, and writes one JSON object
  per frame to the output file (JSONL convention).  Per packet,
  the row carries `format`, `formatId`, `transport`, `dataSize`,
  the relevant `AncMeta` sidecar (line / fieldB for ST 291),
  and either a `parsed` object (when a parser is registered) or
  a `hex` fallback dump.  `Cea708Cdp::toJson()` is special-cased
  to produce structured JSON rather than a string round-trip.
- [x] File-handle lifecycle matches `CaptureStats`: open at
  open() time, close at close() time, write-error latches to
  suppress error-spam.
- [x] `tests/unit/inspector_ancdata.cpp` — 1 case / 76
  assertions covering the full pipeline: TPG → MediaIOPortConnection
  → Inspector → JSONL → parse JSONL → verify per-frame schema
  + sequence counter advance + caption-text round-trip.

**Demo (manual sanity check):**
Driving `mediaplay -s TPG --sc TpgAncCaptionsEnabled:true
--sc TpgAncCaptionsFile:etc/substest.srt --sc TimecodeEnabled:true
-d Inspector --dc InspectorTests:AncData
--dc InspectorAncDataFile:/tmp/out.jsonl`
yields rows like:
```json
{"frame":0,"payloadCount":1,"packets":[{"format":"Cea708",
"formatId":1,"transport":"St291","dataSize":35,"line":11,
"fieldB":false,"parsed":{"captionServiceActive":true,
"ccData":[{"b1":196,"b2":69,"type":0,"valid":true},
{"b1":205,"b2":79,"type":0,"valid":true}],
"ccDataPresent":true,"frameRateCode":5,"identifier":38505,
"sequenceCounter":0,"timeCode":"01:00:00:00",
"timeCodePresent":true}}]}
```
0xC4 → 'D' (parity-stripped), 0x45 → 'E', 0xCD → 'M', 0x4F → 'O' — the
caption text round-trips byte-for-byte through the whole stack.

### AFD + Bar Data

**Files:**
- [ ] `include/promeki/afd.h` + `src/proav/afd.cpp`
- [ ] `tests/unit/proav/afd.cpp`

**Surface:**
- [ ] `Afd::Code` enum mirroring SMPTE 2016-1 (4-bit AFD
  codes).
- [ ] `Afd` value type: `Code code; BarData bars;`.
- [ ] `static Result<Afd> parse(const AncPacket &,
  const AncTranslateConfig &cfg = {})`.
- [ ] `static AncPacket build(const Afd &, AncTransport
  target, const AncTranslateConfig &cfg = {})`.

### ATC LTC / VITC timecode

**Files:**
- [ ] `include/promeki/atc.h` + `src/proav/atc.cpp`
- [ ] `tests/unit/proav/atc.cpp`

**Surface:**
- [ ] `Atc::Kind` enum: `Ltc`, `Vitc1`, `Vitc2`.
- [ ] `static AncFormat formatFor(Kind)` /
  `static Kind kindOf(AncFormat)` — bidirectional mapping
  between Kind and the matching `AtcLtc`/`AtcVitc1`/
  `AtcVitc2` format ID.
- [ ] `static Result<Timecode> parse(const AncPacket &,
  const AncTranslateConfig &cfg = {})` — reuses the existing
  `Timecode` (libvtc-backed). Kind is inferred from
  `packet.format()`.
- [ ] `static AncPacket build(const Timecode &, Kind,
  AncTransport target, const AncTranslateConfig &cfg = {})`
  — uses the codec registered for `(formatFor(kind), target)`.

### SCTE-104

**Files:**
- [ ] `include/promeki/scte104.h`
- [ ] `src/proav/scte104.cpp`
- [ ] `tests/unit/proav/scte104.cpp`

**Surface:**
- [ ] `Scte104Message` value type modelling the operation
  list + protocol version + splice request data. Variant
  type integration: `TypeScte104Message`.
- [ ] `static Result<Scte104Message> parse(const AncPacket &,
  const AncTranslateConfig &cfg = {})`.
- [ ] `static AncPacket build(const Scte104Message &,
  AncTransport target, const AncTranslateConfig &cfg = {})`.
- [ ] Sub-types for the operations promeki cares about
  initially: `SpliceRequest`, `TimeSignal`,
  `SegmentationDescriptor`. Other ops parse to an opaque
  `Variant`-encoded blob — application code that needs them
  can extend the parser later.

### HDR static metadata (SMPTE 2086)

**Files:**
- [ ] `include/promeki/hdrstatic2086.h` +
  `src/proav/hdrstatic2086.cpp`
- [ ] `tests/unit/proav/hdrstatic2086.cpp`

**Surface:**
- [ ] `HdrStatic2086` value type: display primaries, white
  point, max/min luminance.
- [ ] `parse` / `build` against `St291` and `HdmiInfoFrame`
  (DRM InfoFrame type 0x87) codecs.

### HDR dynamic metadata (HDR10+ / ST 2094-40)

**Files:**
- [ ] `include/promeki/hdrdynamic2094_40.h` +
  `src/proav/hdrdynamic2094_40.cpp`
- [ ] `tests/unit/proav/hdrdynamic2094_40.cpp`

**Surface:**
- [ ] `HdrDynamic2094_40` value type modelling the
  application_version / num_windows / targeted_system_display
  / mastering_display fields.
- [ ] `parse` / `build` against `St291` and `HdmiInfoFrame`
  (Vendor-Specific InfoFrame) codecs.

### KLV (MISB ST 0601)

**Files:**
- [ ] `include/promeki/klv0601.h` + `src/proav/klv0601.cpp`
- [ ] `tests/unit/proav/klv0601.cpp`

**Surface:**
- [ ] `Klv0601` value type for the geolocation tags promeki
  cares about (precision timestamp, platform heading,
  sensor LLH, frame center, footprint).
- [ ] `parse` / `build` against `St291` (broadcast carriage)
  and `MpegTsPrivate` (transport-stream carriage) codecs.

---

## Phase 3.5 — Subtitle file I/O + CEA-608 codec

**Status:** Phase 3.5 **landed end-to-end** as of 2026-05-13.
`Subtitle` / `SubtitleList` value types + the SubRip parser/
emitter pair landed 2026-05-11.  `Cea608Encoder` + `Cea608Decoder`
gained pop-on (3.5a/b), paint-on, and roll-up (3.5d) modes plus
the full PAC + mid-row attribute set.  TPG wiring (3.5c) loads
SubRip files at open + drives the encoder at the configured
frame rate.  `Scc` value type (3.5e) + TPG SCC bypass path
(3.5f) landed 2026-05-12.  The `anc-subrip-roundtrip`
functional test (3.5g) landed 2026-05-13 as a `promeki-test`
case (`captions.cea608.subrip_roundtrip`).  A production-
readiness pass on the encoder / decoder pair landed alongside
3.5g: CaptionColor::Black for BG-attribute, isPac mirroring
decodePac's row-11 constraint, doRCL ts-aware paint-on/roll-up
flush, doENM PopOn-gating, multi-row PAC per-row loadingColumn
reset + first-row-only anchor commit, plus stale-doc cleanup
in the encoder / decoder headers.

This phase adds a real subtitle-file driven test framework on
top of the Phase 2b CEA-708 carriage.  Replaces the static
byte-pair TPG injector (which encodes a fixed string every
frame with no timing) with a time-anchored cue source driven
from a SubRip (`.srt`) file, plus the matching CEA-608
encoder/decoder pair to convert cues to / from the
`Cea708Cdp::CcDataList` triples the wire layer already carries.

The motivation is twofold:

1. **Realistic content for the ANC test framework.**  Today's
   TPG injector emits the same caption string every frame as
   raw byte-pairs with no CEA-608 control codes, no pop-on
   framing, no cue boundaries.  That round-trips through the
   wire layer fine but exercises essentially zero of the
   caption stack.  A SubRip-driven source produces real
   timed cues with realistic content — the right input for
   a "real world" test fixture.
2. **The CEA-608 control-code encoder is currently missing.**
   Any real caption interop (capture from a real broadcast,
   delivery to YouTube via SEI, round-trip verification)
   needs a stateful 608 encoder that meters control codes +
   characters across frames.  Phase 3.5 lands that machinery.

CEA-708 typed encode/decode (`Cea708Service` / window manager
/ DTVCC service blocks) is deliberately deferred to Phase 6.
The wire layer is format-agnostic — `cc_data` triples of any
`cc_type` ride through the existing `Cea708Cdp` opaquely —
so 708-typed support adds nothing to the pipeline test, only
to character-set fidelity (Unicode round-trip) and real
broadcast capture interop.  Phase 3.5 is 608-only.

### Generic `Subtitle` / `SubtitleList` value types — **Landed**

**Files:**
- [x] `include/promeki/subtitle.h` + `src/proav/subtitle.cpp`
- [x] `tests/unit/subrip.cpp` (covers both `SubRip` parsing
  and the underlying `Subtitle` / `SubtitleList` API)

**Surface:**
- [x] `Subtitle` value type — single timed cue.  CoW value-type
  handle (pimpl: `SharedPtr<SubtitleImpl>`).  No `::Ptr` alias.
  Fields: `TimeStamp start`, `TimeStamp end`,
  `SubtitleSpan::List spans` (styled runs, see below),
  `SubtitleAnchor anchor` (9-position enum in `enums.h`,
  values 1..9 match ASS `\anN` directly), `Rect2Di32 region`
  (pixel-space bounding box hint, `isValid()` false when
  unset), `String speaker` (voice / accessibility attribution),
  `Metadata metadata` (format-specific extensions).
  `text()` returns the flat concatenation of every span's
  text (cached in the impl).
- [x] `SubtitleSpan` value type — one styled run within a cue.
  Plain (non-pimpl) value type carrying `String text`,
  `bool bold`, `bool italic`, `bool underline`, and an
  optional `Color color` (invalid = inherit renderer
  default).  Variant tag `TypeSubtitleSpan` (`0x5D`).
  Convenience alias `SubtitleSpan::List`.  Added 2026-05-11
  to let the renderer paint mixed-style cues without
  re-parsing inline markup per frame.
- [x] `SubtitleList` value type — ordered collection.  CoW
  value-type handle.  Search helpers: `findActiveAt(t)`,
  `findNextAfter(t)`, `findInRange(start, end)`.  Binary
  search when the cached "sorted by start" flag is set;
  linear-scan fallback otherwise.  `sortByStart()` sets the
  flag; mutators that could break order reset it.
- [x] Variant payload `TypeSubtitle` (tag `0x5C`).  DataStream
  wire format: start, end, anchor value, region, speaker,
  metadata, `List<SubtitleSpan>`.  The redundant flat-text
  field was dropped on the 2026-05-11 span-model conversion
  (no-backwards-compat preference); the flat text accessor
  reconstructs from spans.
- [x] `Metadata::Subtitle` key — for per-frame attribution.
  Stamped on **every** frame in the cue's display window
  (not just the start) so downstream renderers see a stable
  active cue per tick.
- [x] `Cea708_NdiXml` codec needs typed value types still
  pending (Phase 3 follow-on).

**Pimpl rationale:** the `Subtitle` field set carries a
`Metadata` member.  `Metadata` is a `VariantDatabase` and
`Variant` carries `Subtitle` (Variant X-macro).  Inline access
to the impl from the header would force `metadata.h` →
`variantdatabase.h` → `variant.h` → `subtitle.h` cycle.  The
pimpl pattern (forward-declare `SubtitleImpl`, define in
`subtitle.cpp`) breaks the cycle while preserving the CoW
value-type contract.

### `SubRip` parser / emitter — **Landed**

**Files:**
- [x] `include/promeki/subrip.h` + `src/proav/subrip.cpp`

**Surface:**
- [x] Static-only utility class — never instantiated.
- [x] `static Result<SubtitleList> parse(const void *, size_t)`
  + `Buffer` and `String` overloads.  Accepts both CRLF and
  LF endings, optional UTF-8 BOM, optional or omitted
  sequence-number lines, both `,` and `.` as the millisecond
  decimal separator, ASS-style `{\anN}` anchor prefix on the
  text, and the SRT-extension `X1:n X2:n Y1:n Y2:n`
  coordinate suffix on the timecode line.
- [x] **Inline markup → styled spans** (added 2026-05-11).
  Recognised tags: `<i>...</i>`, `<b>...</b>`, `<u>...</u>`,
  `<font color="...">...</font>`, ASS aliases `<em>` /
  `<strong>`, the WebVTT-style `<v Speaker>...</v>` voice
  tag (captured into `Subtitle::speaker`), and `<br>` line
  break.  Unknown tags survive as literal text so a
  parse → emit round-trip stays byte-stable for tags the
  renderer might still want to interpret.  Colour values
  go through `Color::fromString`.
- [x] `static Buffer emit(const SubtitleList &)` + `String`
  overload — canonical CRLF SubRip with re-numbered 1-based
  sequence indices.  Anchor prefix + coordinate suffix
  round-trip byte-stable.  Spans round-trip via canonical
  `<i>` / `<b>` / `<u>` / `<font color="#RRGGBB">` markup.
- [x] Doctest: empty input, three-cue round-trip, LF-only,
  UTF-8 BOM, missing seq line, dotted ms separator, malformed
  timecode / arrow, anchor parse, region parse, sequence
  renumbering, anchor + region round-trip, plus styled-spans
  parse + round-trip and `<v Speaker>` capture.

### `Cea608Encoder` (Phase 3.5a — pop-on only) — **Landed**

**Files:**
- [x] `include/promeki/cea608.h` + `src/proav/cea608.cpp` —
  shared parity helper, CEA-608 control-code constants (RCL,
  EOC, EDM, …), the 7-primary `CaptionColor` palette, and
  the static `encodePac` / `decodePac` / `encodeMidRow` /
  `decodeMidRow` helpers used by both the encoder and the
  decoder.
- [x] `include/promeki/cea608encoder.h` +
  `src/proav/cea608encoder.cpp` — pimpl encoder.
- [x] `tests/unit/cea608.cpp` — PAC + mid-row encode/decode
  helpers (13 cases).
- [x] `tests/unit/cea608encoder.cpp` — encoder schedule +
  pre-roll behaviour (16 cases).

**Surface:**
- [x] `Cea608Encoder` stateful worker (pimpl,
  copy/move-deleted) constructed with `Cea608Encoder::Config`
  `{FrameRate, Mode = PopOn, Channel = CC1, int32_t
  rollUpRows = 2}`.
- [x] `Cea608Encoder::Mode` enum: `PopOn`, `PaintOn`,
  `RollUp` — only `PopOn` implemented; the other two are
  reserved values that surface `Error::NotImplemented` from
  `setSubtitles`.
- [x] `Cea608Encoder::Channel` enum: `CC1`, `CC2`, `CC3`,
  `CC4` — only `CC1` implemented (overwhelmingly common
  English caption case).
- [x] `Error setSubtitles(const SubtitleList &)` — load the
  timeline.  Computes the full per-frame schedule at load
  time; surfaces `Error::Invalid` (bad FrameRate),
  `Error::NotImplemented` (non-PopOn / non-CC1), or
  `Error::OutOfRange` (cue too close to t=0 or overlapping
  prior cue's pre-roll).
- [x] `SubtitleList encodableSubset(const SubtitleList &,
  SubtitleList *outDropped = nullptr) const` — pre-flight
  filter that drops cues whose pre-roll would land before
  frame 0 or overlap the prior cue's EDM, returning the
  encodable subset.  Lets producer code (TPG) feed real-
  world SRT files where the first cue is too close to t=0
  without aborting the whole batch.  Added 2026-05-11.
- [x] `Cea708Cdp::CcDataList nextFrame(FrameNumber) const` —
  returns exactly one CcData triple per call with the
  configured channel's `cc_type` and odd-parity-stamped
  bytes.  Empty frames carry the null pair `(0x80, 0x80)`.
- [x] `FrameNumber earliestStartFor(const Subtitle &) const`
  — diagnostic: returns the RCL frame for a cue, or
  `FrameNumber::unknown()` when pre-roll would land before
  frame 0.  Includes the mid-row + PAC frames in the count
  (consistent with `setSubtitles`).
- [x] Pop-on byte-stream layout: `2 (RCL) + body + 2 (EOC)`
  frames before `cue.start`, then `2 (EDM)` at `cue.end`.
  The `body` is `2 (PAC) + 2*S (mid-row codes for S span-
  style changes) + N (char pairs)`.  Control codes doubled
  per spec; character pairs not doubled.  Odd-length spans
  pad the trailing pair with `0x20` space.
- [x] **PAC is computed dynamically from the cue's anchor +
  first span style** (landed 2026-05-11): row from
  `SubtitleAnchor` (Top* → 1, Middle* → 8, Bottom* → 15);
  colour from the span's `Color::nearestPaletteIndex`
  against `Cea608::palette()`; italic + underline flags
  copied through directly.  Span boundaries with a wire-
  style change emit a doubled `Cea608::encodeMidRow` code.
  Bold logs a one-shot warning per `setSubtitles` and is
  otherwise dropped (608 wire can't carry bold).  Italic on
  the wire is always white per 608 spec; encoder honours
  italic when both italic and a non-white colour are
  requested.  Character set: basic ASCII 0x20..0x7E
  passthrough; newlines / non-ASCII substituted with space.
- [x] Doctest coverage: empty list → null pairs; invalid
  config; per-frame byte-pair stream matches reference for
  2-char + 3-char cues; odd-parity on every emitted byte;
  pre-roll error at t=0 boundary; pre-roll error on tight
  back-to-back cues; comfortably spaced cues schedule
  successfully; `earliestStartFor` diagnostic; deterministic
  output for identical input; `encodableSubset` drops the
  expected cues.

### `Cea608Decoder` (Phase 3.5b — pop-on only) — **Landed**

**Files:**
- [x] `include/promeki/cea608decoder.h` +
  `src/proav/cea608decoder.cpp` — pimpl decoder.
- [x] `tests/unit/cea608decoder.cpp` — 16 cases incl.
  5-case styled round-trip suite against the encoder.

**Surface:**
- [x] `Cea608Decoder` stateful worker (pimpl,
  copy/move-deleted) constructed with `Config { Channel
  channel = CC1 }`.  Channel alias matches
  @ref Cea608Encoder::Channel.
- [x] `void pushFrame(FrameNumber, TimeStamp, const
  Cea708Cdp::CcDataList &)` — feed one frame's worth of
  cc_data triples.  Filters by `cc_type` (field) and by the
  channel bit (bit 3 of the first byte after parity strip);
  parity-stripped bytes dispatch to the pop-on state machine.
- [x] `SubtitleList finalize()` — emit accumulated cues.
  Cues still displayed at finalize close at the most recent
  @ref pushFrame timestamp.  Emitted cues carry the styled
  `SubtitleSpan::List` recovered from PAC + mid-row state,
  and the `SubtitleAnchor` recovered from PAC row.  Resets
  internal state.
- [x] `const String &displayedText() const` — live state
  accessor for renderers that want the on-screen flat text
  between @ref pushFrame calls.
- [x] `Subtitle displayedCue() const` — live state accessor
  that returns the currently displayed cue as a full
  `Subtitle` (spans + anchor), not just the flat text.
  Added 2026-05-11; used by `SubtitleBurnMediaIO` when
  `Cea608Anc` is in the source list so the renderer sees
  the recovered attributes.
- [x] `void reset()` — drop in-flight state without emitting.
- [x] **Pop-on state machine** — tracks non-displayed and
  displayed memory buffers per spec: @c RCL clears
  non-displayed and resets style to defaults; **PAC sets the
  current row + first-span style** (colour from the
  7-primary palette, italic, underline) and records the
  anchor implied by the row; **mid-row codes update colour /
  italic / underline mid-cue**, flushing the current text
  into a styled `SubtitleSpan` and starting a new run;
  character pairs accumulate into the current run's text;
  @c EOC swaps non-displayed → displayed and records the
  cue's @c start at the current frame timestamp; @c EDM
  finalizes the displayed cue with @c end at the current
  frame timestamp; @c ENM clears non-displayed.
- [x] **Robustness behaviours**: doubled control codes collapse
  to a single occurrence (the second is treated as a no-op);
  a character pair between two identical control codes
  breaks the "consecutive" rule and the second is processed
  again; parity-failed bytes are skipped (treated as null);
  null pairs (0x80 0x80 on wire) are no-ops; unknown
  control codes are silently ignored.
- [x] Doctest: round-trip with @ref Cea608Encoder for 1 cue,
  3 sequential cues, and a longer-text cue (13 chars);
  hand-rolled RCL/PAC/AB/EOC/EDM cycle; doubled-control
  collapse; parity-fail skip; finalize closes still-
  displayed cue at last-pushed ts; cc_type filter; reset;
  **styled round-trips**: anchor recovery from PAC row,
  italic + underline preservation, 7-colour palette
  quantization, mid-row colour change between spans, bold
  drop-and-warn.

### Encoder offset correction (2026-05-11)

The encoder was originally landed with EOC scheduled at
`startFrame - 2` and `startFrame - 1` (so the receiver swap
happened 2 frames before the cue's logical start).  When the
decoder landed, it became clear the natural contract is for
the **first** @c EOC to fire **at** @c startFrame so the cue's
visible window is exactly `[startFrame, endFrame)`.  The
encoder's `firstFrame` calculation was updated from
`startFrame - (RCL + PAC + chars + EOC)` to
`startFrame - (RCL + PAC + chars)` — the EOC pair lands at
`startFrame` and `startFrame + 1` instead of `startFrame - 2`
and `startFrame - 1`.  Total schedule span is unchanged
(6 + N frames per cue); the pre-roll constraint between cues
is unchanged; tests updated.

### TPG wiring swap (Phase 3.5c) — **Landed**

**Files:**
- [x] `include/promeki/mediaconfig.h` — key changes.
- [x] `include/promeki/tpgmediaio.h` +
  `src/proav/tpgmediaio.cpp` — per-frame integration.
- [x] `include/promeki/subtitle.h` +
  `src/proav/subtitle.cpp` — `SubtitleList::snapToFrames`
  helper.
- [x] `tests/unit/tpg_anc_captions.cpp` +
  `tests/unit/inspector_ancdata.cpp` — rewritten for the
  SubRip-driven flow.

**Changes:**
- [x] **Removed** `MediaConfig::TpgAncCaptionsText`
  (String).  Per the no-backwards-compat preference, no shim.
- [x] **Added** `MediaConfig::TpgAncCaptionsFile` (String —
  path to a `.srt` file, resolved through `File`).  Empty
  default means "no file" — the TPG still emits per-frame
  null-pair CDPs.
- [x] **Added** `MediaConfig::TpgAncCaptionsOffset` (Duration)
  — applied to every cue's @c start / @c end before
  scheduling.  Positive values delay captions relative to the
  TPG's t=0 frame timeline; negative values advance them.
  Use case: SubRip files authored against a broadcast hour-
  of-day TC that need to be re-anchored to the TPG's t=0.
- [x] Kept `TpgAncCaptionsEnabled` (bool) and
  `TpgAncCaptionsLine` (int) unchanged.
- [x] `TpgMediaIO::executeCmd(Open)` reads the SubRip file,
  applies the offset, snaps every cue to the nearest frame
  boundary via @ref SubtitleList::snapToFrames, sorts by
  start, instantiates a `Cea608Encoder` with the configured
  rate, runs @ref Cea608Encoder::encodableSubset to drop
  cues whose pre-roll lands before t=0 or overlaps a prior
  cue's EDM (warning per dropped cue), and calls
  `setSubtitles` on the filtered subset.  Open surfaces:
  `Error::IOError` (file open / short read), `Error::ParseFailed`
  (malformed SubRip), `Error::Invalid` (bad rate),
  `Error::NotImplemented` (non-PopOn / non-CC1 mode — not
  currently surfaceable since the TPG hardcodes the defaults).
  The pre-2026-05-11 `Error::OutOfRange` behaviour for tight
  cues is gone — dropped cues now log + continue instead of
  failing open.
- [x] `TpgMediaIO::executeCmd(Read)` pulls the per-frame
  @c CcDataList from the encoder, wraps it into a `Cea708Cdp`
  with the configured sequence counter / timecode section,
  builds an @ref AncPacket via the `_ancTranslator`, and
  attaches it to a fresh `AncPayload` on the produced Frame.
  Empty (null-pair) byte streams still produce a valid CDP
  every frame so receivers see a steady caption-service-
  active signal.
- [x] **Per-frame `Metadata::Subtitle` stamp** (updated
  2026-05-11).  For every frame whose timestamp falls inside
  any kept cue's `[start, end)` window the TPG sets
  @c Metadata::Subtitle on the Frame to a `Variant(Subtitle)`
  carrying the active cue.  Downstream renderers
  (`SubtitleBurnMediaIO`) and MediaIO sinks that don't want
  to decode ANC read the cue directly.  The earlier
  "stamp only on the start frame" design served edge-
  triggered consumers; render pipelines need the stamp on
  every active frame (`Subtitle` is a CoW handle so per-
  frame restamping is free).
- [x] **Frame snapping at config time.** Added
  @ref SubtitleList::snapToFrames(const FrameRate &) returning
  a new list with each cue's @c start / @c end rounded to the
  nearest frame boundary at the given rate.  Exact rational
  math (via @ref FrameRate::cumulativeTicks) avoids drift on
  NTSC rates.  Both the encoder's schedule and the TPG
  metadata stamp use the same snapped list, so they stay in
  lockstep.
- [x] Test coverage:
  - `tpg_anc_captions.cpp` — disabled / no-file / file-driven /
    offset shift / malformed file (`ParseFailed`) / missing
    file / `Metadata::Subtitle` stamped on every frame in
    the cue window (start, mid, last-frame-of-window) /
    sequence counter / MediaDesc advertisement / VANC-line
    carry-through / SRT-anchor + styled-spans preservation
    through TPG / `etc/substest.srt` per-cue anchor
    preservation.
  - `inspector_ancdata.cpp` — full pipeline (TPG → port
    connection → Inspector → JSONL) with SubRip-driven cues;
    reconstructs caption text from cc_data triples across
    multiple JSONL rows.

### Paint-on + roll-up modes (Phase 3.5d) — **Landed 2026-05-12**

- [x] Encoder paint-on: RDC opens direct writes to displayed
  memory.  Per-cue layout
  `[RDC,RDC, PAC,PAC, (chars|MR,MR)..., EDM,EDM]`; pre-roll =
  4 frames (doubled RDC + doubled PAC), chars stream live
  starting at `startFrame`.  Mid-row codes still emitted for
  style runs.  EDM doubled at the cue's `endFrame`; the same
  EDM-elision policy as pop-on rescues densely-packed cues.
- [x] Encoder roll-up: RU2/RU3/RU4 selects roll-up row count
  (clamped to [2,4]).  RUx pair emitted **once per batch** at
  the start; subsequent cues skip the RUx and just emit
  `[CR,CR, PAC,PAC, chars/MR...]`.  PAC row forced to 15 per
  spec (roll-up is bottom-anchored regardless of cue anchor).
  No per-cue EDM — cues scroll off when the next CR fires.
- [x] Decoder paint-on + roll-up: matching state machines.
  `currentMode` tracks the most recently seen mode-establishing
  control code (RCL / RDC / RU2-4).  RDC switches to paint-on
  (chars commit live, EDM finalises the cue with `start = PAC
  ts`); RUx switches to roll-up (CR finalises the current row
  with `start = preceding CR ts`, `end = this CR ts`).
  `displayedText` / `displayedCue` mirror the loading buffer
  in paint-on/roll-up modes so renderers see live state.
- [x] Per-mode doctest plus one round-trip test per mode —
  6 encoder tests (paint-on byte layout, paint-on overrun /
  t=0 pre-roll, roll-up first cue / subsequent cue / overrun /
  rollUpRows clamping) + 4 decoder tests (hand-rolled RDC and
  RU2/CR cycles, encoder/decoder round-trip for each mode).

### `Scc` value type (Phase 3.5e) — **Landed 2026-05-12**

**Files:**
- [x] `include/promeki/scc.h` + `src/proav/scc.cpp`
- [x] `tests/unit/scc.cpp` (15 cases)

**Surface:**
- [x] `Scc` plain value type holding `LineList` (= `List<Line>`)
  where `Line` is `{Timecode start, List<uint16_t> bytePairs}`
  (uint16_t per the SCC convention: high byte = field-1 first
  byte, low byte = field-1 second byte; bytes already carry
  odd parity).
- [x] `static Result<Scc> fromBuffer(const void *, size_t)`
  + `Buffer` and `String` overloads — parse the
  `Scenarist_SCC V1.0` header + per-line `HH:MM:SS;FF\t
  XXXX XXXX ...` rows.  Accepts CRLF or LF line endings,
  tolerates UTF-8 BOM, accepts both `:` (NDF) and `;` (DF)
  separator before the frame digits.  Surfaces
  `Error::ParseFailed` on missing header, malformed timecode,
  malformed byte pairs, or missing tab between TC and bytes.
- [x] `Buffer toBuffer() const` (+ `String toString()`) — emit
  canonical SCC with CRLF endings, the `Scenarist_SCC V1.0`
  header + blank line, lower-case 4-hex-digit byte-pair
  groups separated by single spaces.
- [x] DataStream operators (`TypeScc` tag `0x5E`).  Wire
  format encodes the timecode digits + DF bit directly
  instead of round-tripping through `Timecode::toString` —
  the canonical `HH:MM:SS:FF` form loses the libvtc Mode
  pointer on re-parse, breaking exact equality.
- (No Variant X-macro integration yet — `Scc` is consumed
  inside the TPG by direct API call, not via a `Variant`.
  Adding it is a one-line follow-on if needed.)
- [x] Doctest: parse canonical 2-row file (NDF), drop-frame
  TC, LF-only endings, trailing row without newline, UTF-8
  BOM, empty input, missing header, malformed TC, malformed
  byte pair, missing tab, emit canonical form, round-trip
  through `fromString` / `toString`, round-trip through
  DataStream operators.

### TPG SCC bypass path (Phase 3.5f) — **Landed 2026-05-12**

- [x] `MediaConfig::TpgAncCaptionsScc` (String — path to a
  `.scc` file, optional; mutually exclusive with
  `TpgAncCaptionsFile` — both set returns
  `Error::InvalidArgument` from open).  When set, TPG
  **bypasses** the `Cea608Encoder` and feeds the SCC
  byte-pairs straight into the `Cea708Cdp::CcDataList` for
  each frame.  The first SCC row anchors to TPG frame 0;
  subsequent rows shift by the absolute frame-count delta
  from the first row.  Frames without an SCC byte pair emit
  the parity-stamped null pair `(0x80, 0x80)` so the
  receiver sees a steady stream.  This is the "real
  broadcast captioner output" test path — proves the CDP
  wire layer alone.
- [x] Doctest: SCC byte pairs land in `CcData` at the
  matching frame indices, file/SCC mutual exclusion returns
  `Error::InvalidArgument`, malformed SCC returns
  `Error::ParseFailed` on open.

### Encoder / decoder production-readiness pass — **Landed 2026-05-13**

Polish round that landed alongside the Phase 3.5g functional
test.  Fixes spec-conformance gaps that didn't surface in
short unit-test cues but mattered for the 23-cue, multi-mode,
multi-channel fixture run end-to-end.

**Spec / robustness fixes** (`src/proav/cea608.cpp`,
`src/proav/cea608decoder.cpp`):
- [x] `Cea608::isPac` mirrors `decodePac`'s row-11 constraint
  — `b1 = 0x10` with `b2`'s bit 5 set (no second-of-pair
  partner for row 11) is now rejected by `isPac` instead of
  being accepted only to fall through `decodePac` as a no-op.
  Sentinel test added in `tests/unit/cea608decoder.cpp`.
- [x] `Cea608Decoder::doRCL` now takes a `TimeStamp` and
  finalises any in-flight paint-on / roll-up cue at that ts
  before clearing.  A wild captioner stream that mode-flips
  mid-air no longer drops the prior cue.  (Encoder dispatcher
  always emits an EDM before re-entering pop-on; the decoder
  now also handles the wild-stream case.)
- [x] `doENM` gated to PopOn — in paint-on / roll-up the
  loading buffer is the live displayed cue, not non-displayed
  memory, so ENM in those modes is now a no-op per spec
  instead of erasing live chars.
- [x] Multi-row PAC handling: `loadingColumn` resets per-PAC
  (was: only set on the first PAC) and the cue's anchor
  commits off the first row's row + column only via a new
  `loadingOnFirstRow` flag.  Tab Offsets after the second
  row's PAC no longer accumulate onto the first row's column.

**`CaptionColor::Black` for BG-attribute round-trip**
(`include/promeki/cea608.h`, `src/proav/cea608.cpp`,
`src/proav/cea608encoder.cpp`):
- [x] `Cea608::CaptionColor` extended with `Black = 7` for
  the EIA-608-B BG-attribute wire index 7 (previously lossy
  round-trip through White).
- [x] `CaptionColorCount = 8`, plus new
  `FgCaptionColorCount = 7` constant for fg-only iteration
  tests (PAC + mid-row colour subfield reserves code 7 for
  italic-white).
- [x] `palette()` now returns 8 entries (Black at index 7).
- [x] `encodeBgAttribute` round-trips Black directly;
  `decodeBgAttribute` no longer maps wire 7 → White.
- [x] `encodePac` / `encodeMidRow` `colorSubfield` treats
  Black as White (fg paths have no Black encoding).
- [x] Encoder's `wireStyleFor` coerces Black fg → White so
  positioning predicates (`hasFirstSpanStyle`) don't degrade
  Center anchors to flush-left for what is wire-equivalent
  to White.
- [x] Two new doctest cases in `cea608decoder.cpp`: Black bg
  round-trip via wire index 7, Black fg silently → White on
  wire with anchor preserved.

**Stale documentation cleanup** in
`include/promeki/cea608encoder.h` and
`include/promeki/cea608decoder.h`:
- [x] `Channel::CC2/CC3/CC4` no longer marked "Reserved" —
  all four supported.
- [x] `Mode` doc reflects per-cue mode mixing.
- [x] Decoder doc lists BS / DER / FON / TR / RTD with their
  real semantics (was: "Unknown control codes ignored in v1").
- [x] Character-set blurb in encoder doc reflects `Cea608Ext`
  coverage (G0 + Special + Extended) instead of "anything
  outside basic ASCII → space".
- [x] `setSubtitles` `@return` references `encodableSubset`
  instead of the obsolete "non-PopOn / non-CC1 surfaces
  NotImplemented".
- [x] Decoder Config doc reflects multi-channel support.

**Test counts:** unit suite was 6375 baseline; now 6381
(+4 robustness regression tests + 2 Black bg tests).  All
6381 pass.  The new `captions.cea608.subrip_roundtrip`
functional test passes.

### Functional test — anc-subrip-roundtrip (Phase 3.5g) — **Landed 2026-05-13**

Landed as a `promeki-test` case rather than a standalone
`tests/func/anc-subrip-roundtrip/` folder.  The promeki-test
runner already provides the per-case scaffolding (TestParams,
TestContext, per-case folder + result.json) the original plan
called for, so the case became a self-contained TestCase
inside the runner instead of a separate binary.

**Files:**
- [x] `utils/promeki-test/cases/captions.cpp` — registers
  `captions.cea608.subrip_roundtrip` via the
  `registerCaptionsCases()` hook in `cases.h`, called from
  `main.cpp`.
- [x] `utils/promeki-test/CMakeLists.txt` — adds the new
  source + `PROMEKI_SOURCE_DIR` compile-def for fixture
  resolution.
- [x] `etc/substest.srt` — vendored SubRip fixture covering
  every parser branch (23 cues across ~1m53s: ASCII / multi-
  line / `{\anN}` anchors / X1..Y2 coordinate hints / inline
  `<i>`/`<b>`/`<u>`/`<font>` markup / UTF-8 / period-as-ms
  separator / WebVTT `<v Speaker>` tags / quotes-and-dashes /
  long single-line / three-line block / leading whitespace).
  Also exercised by two doctest cases in
  `tests/unit/subrip.cpp` (parse every branch + canonical
  round-trip).

**Flow (three stages, all in-process):**
- [x] Stage 1: in-process pipeline TPG
  (`TpgAncCaptionsFile = etc/substest.srt`, VideoFormat pinned
  to `Smpte1080p30`) → `InspectorMediaIO`
  (`InspectorTests = AncData`, `InspectorAncDataFile =
  <testFolder>/anc.jsonl`).  Runs 1080p30 × 145s = 4350
  frames, covering the full ~1m53s fixture window plus tail
  for the last over-cap cue's auto-split sub-cues.
- [x] Stage 2: case-internal reconstructor walks the JSONL,
  parses each row's `packets[].parsed.ccData` triples through
  `Cea608Decoder::pushFrame`, calls `finalize()` to get the
  recovered `SubtitleList`.
- [x] Stage 3: emits `SubRip::emit(recovered)` to
  `<testFolder>/roundtrip.srt` for post-mortem diff, then
  asserts every ASCII source cue has a decoded match in its
  display window with whitespace-normalised text equality and
  same vertical anchor band.

**Verified pass:** 21/21 ASCII source cues matched, 0 text
mismatches, 0 vertical-band mismatches, 4350 JSONL rows
parsed, 25 recovered cues (extras from the auto-split path
for the over-cap fixture cue).

**Deliberate compromises documented inline:**
- Anchor compare is **vertical-half only** (Top / Middle /
  Bottom).  608 horizontal recovery is intrinsically lossy
  for cues whose width approaches 32 cells (Center on a
  30-char cue lands at column 1, decoder's `col < 4 → Left`
  pulls it to the Left variant).  Strict horizontal
  preservation is already covered by `cea608encoder.cpp`
  unit tests with short cues.
- VideoFormat pinned to 1080p30 (not the suite default
  720p59.94) so the case's per-frame `TimeStamp` math matches
  the encoder's frame schedule.

**Not implemented from the original plan (handled elsewhere
or out of scope here):**
- The "Stage 3 byte-exact diff against `etc/substest.srt`"
  was replaced with per-cue text + vertical-anchor matching
  because the encoder intentionally re-times cues onto frame
  boundaries (snapToFrames) and the auto-split overflow path
  produces a different cue count than the source file.
- The "repeat through RTP-40" wire-leg test is covered by
  the Phase 6 functional matrix entry below
  (`tests/func/anc-rtp40-roundtrip/`).

### Things Phase 3.5 deliberately leaves out

- **CEA-708 typed (DTVCC) encode/decode.**  Deferred to Phase
  6.  Reserved names: `Cea708Service` (the typed Variant
  payload for a single 708 service block) and
  `Cea708WindowState` (the window manager state).  The wire
  layer doesn't need this; only Unicode round-trip and real
  broadcast 708 capture interop do.
- **Styled output rendering.**  No paint engine integration
  here.  Phase 6 `Cea708Overlay` MediaIO is the place for
  visible burned-in captions.
- **WebVTT.**  Out of scope.  SubRip is the round-trip target
  for `.srt` files; WebVTT (`.vtt`) is a separate text format
  with styling that maps roughly to 708 service blocks, not
  608.

---

## Phase 4 — MediaIO backend integration

**Status:** Pending.  The framework (Phase 2) and the
first-class CEA-708 codec (Phase 2b) are landed, so each
backend's ANC seam is now a small mechanical addition.  TPG
ingest and Inspector egress are already plumbed (Phase 2b),
proving the integration shape.  The remaining backends —
NdiMediaIO and RtmpMediaIO source/sink ANC paths — wait on
the Phase 3 typed value types they need on the wire
(`Cea708_NdiXml` codec needs an XML carriage agreement;
`Cea708_RtmpAmf` codec needs AMF0 `onCaptionInfo` decode).

ANC packets entering and leaving the existing backends. By
this phase the registries are mature enough that each
backend's ANC seam is small: enumerate frame's ANC packets,
emit those whose transport matches natively as-is, translate
the rest at the seam, skip ones with no translator (log once
per format).

### TpgMediaIO source — landed early (Phase 2b, extended in Phase 3.5c, attribute pass-through 2026-05-11)

- [x] `TpgAncCaptionsEnabled` / `TpgAncCaptionsFile` /
  `TpgAncCaptionsOffset` / `TpgAncCaptionsLine` config keys
  (SubRip-driven; see Phase 3.5c).
- [x] Per-frame CEA-708 ANC injection via `Cea608Encoder` →
  `Cea708Cdp` → `AncTranslator::build`.  The encoder now
  carries the cue's anchor (as PAC row), span styles (italic /
  underline / colour quantised to 7-primary palette), and
  mid-row codes for span-boundary style changes.
- [x] `Cea608Encoder::encodableSubset` pre-flight drops cues
  whose pre-roll would land before t=0 or overlap a prior
  cue's EDM, with one warn per dropped cue; the rest still
  encode.  Open no longer fails on tightly packed SubRip
  files.
- [x] Per-frame `Metadata::Subtitle` stamp on every frame in
  every kept cue's display window — feeds the
  `Metadata`-source branch of `SubtitleBurnMediaIO`.
- [x] `mediaDesc().ancList()` advertises the produced ANC
  stream.
- [ ] Unify with future `AncEmission` EnumList key when ATC /
  AFD / HDR codecs join (Phase 6 cleanup).

### InspectorMediaIO sink — landed early (Phase 2b)

- [x] `InspectorTest::AncData` test + `InspectorAncDataFile`
  config key.
- [x] Per-frame JSONL dump of every ANC packet, with
  `AncTranslator::parse` decode + `Cea708Cdp::toJson()`
  structured output + hex fallback for unparseable formats.

### NdiMediaIO

NDI metadata frames carry the closed-caption / AFD / timecode
equivalents as XML.

- [ ] **Source side:** for each incoming NDI metadata frame,
  identify the format via the top-level XML element (build a
  table or use a parser hook). Build an `AncPacket` with
  `transport = NdiXml`, `data = <XML bytes>`,
  `meta = {NdiXml::ElementName = "..."}`. Attach to the
  outbound `Frame`'s `AncPayload`. Unknown XML elements are
  kept as-is (transport=NdiXml, format=Invalid with a
  user-registered fallback if one exists).
- [ ] **Sink side:** walk `frame.ancPayloads()`; for each
  packet, if `transport == NdiXml`, emit the bytes verbatim
  (byte-exact passthrough). Otherwise look up
  `AncTranslator(packet.format(), packet.transport(), NdiXml)`
  and emit the result. Packets with no available translator
  are skipped (logged once per format-id).
- [ ] Tests: NDI ANC round-trip (NDI → NDI byte-exact),
  cross-format (RTP-40 St291 captions → NDI XML captions).

### RtmpMediaIO

RTMP metadata carries via AMF0 script tags
(`onMetaData`, `onCaptionInfo`, `onCuePoint`).  **For YouTube,
Twitch, and most modern CDN ingest, this path is not what the
service actually reads** — see the `Cea708 ← / → HlsSei` codec
section below, which is the practical caption delivery path
for H.264-bearing transports.  The AMF path is still useful
for Facebook Live and older OBS-derived endpoints.

- [ ] **Source side:** parse incoming `onCaptionInfo`,
  `onMetaData`, `onCuePoint` script tags into `AncPacket`
  with `transport = RtmpAmf`, `data = <AMF0 bytes>`,
  `meta = {RtmpAmf::ScriptName = "..."}`. Resolve format
  via the script-name table (`onCaptionInfo → Cea708`,
  `onCuePoint → Scte104`, etc.).
- [ ] **Sink side:** mirror — packets with `transport ==
  RtmpAmf` ride through unchanged; others go through
  `AncTranslator(format, src, RtmpAmf)`.
- [ ] Tests: RTMP round-trip via the existing `RtmpClient`
  loopback test fixture.

### Codec base classes — Frame-shaped push/pull + ANC pairing helpers — **In working tree 2026-05-12 (uncommitted)**

Prerequisite for the NVENC SEI injection step.  Before NVENC can
walk `frame.ancPayloads()` and emit SEI it has to *see* the source
Frame's ANC at all — the old `submitPayload(payload)` /
`receiveCompressedPayload()` shape only handed the encoder a bare
image.  The four codec bases were widened to push/pull whole
Frames and given the helpers that every concrete backend would
otherwise have to reimplement.

**Surface (`VideoEncoder` / `VideoDecoder` / `AudioEncoder` /
`AudioDecoder`):**

- [x] `Error submitFrame(const Frame &frame)` — replaces the
  payload-only entry points.  Concrete backends pull the input
  payload they want via `selectInputPayload` and hold the source
  Frame so its audio / ANC / Frame-level metadata can echo
  through to the matching output Frame.
- [x] `Frame receiveFrame()` — replaces
  `receiveCompressedPayload` / `receiveVideoPayload` /
  `receiveAudioPayload`.  Returns a fully-paired output Frame.
- [x] `configure(const MediaConfig &)` is now **non-virtual** on
  the base.  It stashes the most-recently-passed config on the
  encoder (reachable via `config()`) and dispatches to a new
  virtual `onConfigure(const MediaConfig &)` hook that concrete
  backends override.  Guarantees `config()` is current for the
  base-class helpers regardless of what a backend's `onConfigure`
  body does.
- [x] `static UncompressedVideoPayload::Ptr
  VideoEncoder::selectInputPayload(const Frame &, int
  streamIndex = -1)` and the matching `CompressedVideoPayload`,
  `PcmAudioPayload`, `CompressedAudioPayload` selectors on the
  other three bases — walk the frame's payload list and return
  the first payload of the expected (un)compressed shape with a
  matching `streamIndex`.
- [x] `static AncPacket::List
  VideoEncoder::selectAncForSei(const Frame &, int
  pairedVideoStreamIndex, const AncFormat::IDList
  &allowedFormats)` — the encoder-side bridge to the new
  `AncDesc::pairedVideoStreamIndex`.  Returns the subset of
  `frame.ancPayloads()` that this encoder owns (matching paired
  index, or unbound `-1`) and whose packet format is in the
  allowed list (empty = disabled).  Foundation for the NVENC
  SEI hook below.
- [x] `static void VideoDecoder::attachExtractedAnc(Frame &,
  AncPacket, int pairedVideoStreamIndex)` — symmetric helper
  for decoders that recover ANC from the compressed bitstream
  (e.g. CEA-708 SEI on H.264 decode).  Finds or creates an
  `AncPayload` on the output Frame keyed to the right paired
  index and appends the packet.
- [x] `static Frame buildOutputFrame(const Frame &source,
  CompressedVideoPayload::Ptr emitted)` (and matching
  uncompressed-video / compressed-audio / pcm-audio overloads)
  — assembles a fresh output Frame, echoing the source's audio /
  ANC / metadata through and adding the emitted payload.
  Replaces the per-MediaIO `_pendingSrcFrames` queues that
  previously paired packets back to inputs by hand.

**Migrations landed in working tree:**

- [x] In-tree encoders: `NvencVideoEncoder`, `JpegVideoEncoder`,
  `JpegXsVideoEncoder`, plus the libfdk-aac and libopus
  `AudioEncoder` subclasses — all expose `onConfigure` +
  `submitFrame` + `receiveFrame`, stash the source Frame on
  submit, and emit pre-paired output Frames via
  `buildOutputFrame`.  NVENC + NVDEC track the source Frame
  alongside their per-slot / per-packet state queues so
  out-of-order / multi-input-per-packet codecs still pair
  correctly.
- [x] In-tree decoders: `NvdecVideoDecoder`, `JpegVideoDecoder`,
  `JpegXsVideoDecoder`, plus the libfdk-aac and libopus
  `AudioDecoder` subclasses — same migration.
- [x] `VideoEncoderMediaIO` / `VideoDecoderMediaIO` /
  `AudioEncoderMediaIO` / `AudioDecoderMediaIO` wrappers — the
  per-MediaIO `_pendingSrcFrames` queue and the
  `_multiImageWarned` / `_multiTrackWarned` bookkeeping are
  gone; the wrapper now just hands the whole Frame to
  `submitFrame` and pushes the encoder's emitted Frames onto
  its output queue.  Audio-only / video-only Frames pass through
  cleanly because `selectInputPayload` returns a null Ptr and
  the wrapper builds an empty-but-valid pass-through via
  `buildOutputFrame`.
- [x] One-shot consumers — `InspectorMediaIO::decompressImages`,
  `SDLPlayerMediaIO`, `ImageFileIO_JPEG` / `_JpegXS::save`,
  `MjpegStreamMediaIO::encodeFrame`, and the
  `tests/func/nvenc/main.cpp` benchmark harness — all wrap
  the submitted payload in a transient Frame and unwrap the
  emitted output Frame.  `tests/unit/codectesthelpers.h` grew
  `frameWith` / `firstCompressedVideo` / `firstUncompressedVideo`
  / `firstCompressedAudio` / `firstPcmAudio` helpers so the
  per-codec unit tests stay one-liners.

**Pending follow-ons before commit:**

- [ ] Land the changeset.  The whole stack (library + all unit
  test executables) compiles clean against `build` /
  `build tests` as of 2026-05-12 late; the user has not yet
  asked for a commit.
- [ ] Doxygen sweep over the new public API — class-level
  paragraphs on each codec base, the four helpers, and the
  paired-index AncDesc fields.  Most landed inline with the
  refactor; a `doxygen-review` pass before commit catches
  anything stale.

### `Cea708 ← / → HlsSei` codec + NVENC SEI injection — **Landed 2026-05-12 (working tree, uncommitted)**

This is the practical caption delivery path for YouTube Live,
Twitch, and any other modern CDN that ingests H.264 over RTMP /
HLS / SRT / DASH.  CDNs read captions from **H.264 SEI NAL
user_data** (ATSC A/53 `user_data_registered_itu_t_t35`,
country=0xB5, provider=0x0031, user_data_type_code=0x03)
wrapping the same `cc_data` triples that `Cea708Cdp` already
carries.

This sits at Phase 4 rather than Phase 6 (where it was
originally sketched as a "future hook") because YouTube
support is a real near-term goal and the SEI mechanism is
H.264-specific (not HLS-specific) — the same SEI inside H.264
NAL units rides transparently over RTMP, HLS, DASH, and
SRT-with-TS.  The existing `AncTransport::HlsSei` enum value
is the carrier; the name is a historical misnomer worth
revisiting (`H264Sei` would describe it better), but renaming
is a follow-on cleanup, not a prerequisite for this phase.

**Files:**
- [x] `src/proav/anccodec_cea708_hlssei.cpp` (separate TU
  from `anccodec_cea708.cpp` so the SEI wrapper logic stays
  isolated from the St291 codec)
- [x] `tests/unit/anccodec_cea708_hlssei.cpp` (13 cases)
- [x] NVENC integration in `src/proav/nvencvideoencoder.cpp`
- [x] Three new caption-SEI cases appended to
  `tests/unit/nvencvideoencoder.cpp` (kept beside the existing
  NVENC tests rather than spinning up a new TU)

**Codec surface (landed):**
- [x] `Cea708 ← HlsSei` parser: validates the ATSC A/53
  wrapper (country=0xB5 USA, provider=0x0031 ATSC,
  user_identifier="GA94", user_data_type_code=0x03), extracts
  `cc_count` (5 bits) cc_data triples, builds a minimal
  `Cea708Cdp` carrying only the cc_data (frame-rate code
  defaulted to 0; the SEI doesn't carry the rest of the CDP
  metadata).
- [x] `Cea708 → HlsSei` builder: takes a
  `Variant(Cea708Cdp)`, extracts the cc_data triples,
  packages them into the ATSC A/53 SEI structure (10-byte
  header + cc_count×3-byte triples + trailing marker),
  returns an `AncPacket` with `transport = HlsSei` and the
  SEI payload bytes as `data`.  Caps at 31 triples per packet
  (5-bit cc_count field) and surfaces `Error::OutOfRange` on
  overflow.
- [x] Per-codec doctest covering: capability queries, zero-
  triple canonical layout, 2-triple wire layout (including
  the `0xF8 | cc_valid<<2 | cc_type` byte packing), AncPacket
  shape (transport + format), overflow rejection, full
  round-trip recovering cc_data triples, malformed-input
  rejection (wrong country code, wrong provider code, wrong
  user_identifier, wrong user_data_type_code, truncated
  cc_data, too-short packet), and `AncTranslator::translate`
  going St291 ↔ HlsSei via the parse + build fallback path.

**NVENC SEI injection hook — Landed 2026-05-12 (working tree, uncommitted):**

The pieces it needs were all in place after the codec-base
refactor above:

- The encoder receives the source Frame on every `submitFrame`
  call (stashed in `Slot::sourceFrame` until the matching
  bitstream comes back).
- TPG already stamps `AncDesc::pairedVideoStreamIndex = 0` on
  the CEA-708 stream it emits.
- `VideoEncoder::selectAncForSei(frame, _streamIndex,
  {AncFormat::Cea708})` returns exactly the packets this
  encoder should translate to SEI.
- `AncTranslator(Cea708, packet.transport(), HlsSei)` already
  produces the ATSC A/53 SEI payload bytes (codec landed
  2026-05-12).

What landed:

- [x] Inside `NvencVideoEncoder::Impl::submitFrame`, call
  `VideoEncoder::selectAncForSei(source, /*streamIndex=*/0,
  {AncFormat::Cea708})` and run each packet through the held
  `AncTranslator` to get its `HlsSei` payload bytes.  The bytes
  land in two new per-slot vectors — `Slot::captionSeiPayloads`
  (owns the bytes) and `Slot::captionSeiArray` (parallel
  `NV_ENC_SEI_PAYLOAD` descriptors pointing into them) — so
  storage outlives the async encode call exactly the way
  `Slot::nvMd` / `nvCll` already do for HDR SEI.
- [x] Wire the slot's caption-SEI buffers into
  `NV_ENC_PIC_PARAMS_H264::seiPayloadArray` /
  `seiPayloadArrayCnt` (and the matching HEVC fields).  AV1 is
  skipped — NVENC has no AV1 caption-OBU path; a one-shot
  warn at configure time tells the caller.  Payload type is `4`
  (`user_data_registered_itu_t_t35` per H.264 / HEVC Annex D);
  NVENC adds the SEI message header + emulation prevention.
- [x] **B-frame display-order pairing** — solved by attaching
  the SEI array to the *input* slot at submit time.  NVENC
  binds `seiPayloadArray` to the input picture, so the SEI
  rides with the encoded picture for that input regardless of
  decode-order reordering.  Same display-order behaviour the
  existing HDR-SEI path relies on.
- [x] Config key `MediaConfig::VideoSeiCaptionsEnabled` (bool,
  **default `true`**).  Default-on is safe because the feature
  is a silent no-op when the source Frame carries no matching
  CEA-708 ANC: `selectAncForSei` returns an empty list and
  no SEI bytes are written.  No `VideoSeiCaptionFormats`
  EnumList — `AncFormat` is a `TypeRegistry`, not a
  `TypedEnum`, so the format set is hard-coded to
  `{AncFormat::Cea708}` for now.  When the second SEI-bearing
  format lands (HDR dynamic metadata, KLV) the gate generalises
  to a list-typed key.
- [x] **Unit tests** — three new cases in
  `tests/unit/nvencvideoencoder.cpp`:
    1. `caption-SEI injection wraps Cea708 ANC into ATSC A/53
       SEI` — builds an `AncPayload` with two known cc_data
       triples (`{true, 0, 0xC4, 0x45}` and `{true, 2, 0x80,
       0x80}`), submits a Frame carrying it through NVENC H.264,
       searches the encoded NAL bitstream for the
       `0xB5 0x00 0x31 0x47 0x41 0x39 0x34 0x03` ATSC A/53
       wrapper marker, then byte-asserts the cc_count flag
       (`0xC2`), em_data (`0xFF`), both triple headers and
       payloads, and the trailing marker.
    2. `caption-SEI suppressed when VideoSeiCaptionsEnabled =
       false` — same setup, opt-out, marker absent.
    3. `caption-SEI silent passthrough when no Cea708 ANC on
       frame` — default-on, no ANC payload, marker absent.

**Functional test:**
- [ ] `tests/func/anc-youtube-sei-roundtrip/` — TPG with a
  SubRip caption file → NVENC encode → RTMP out to a local
  loopback server → tcpdump / ffmpeg-decode of the resulting
  FLV → SEI extraction → `Cea708 ← HlsSei` parser →
  `Cea608Decoder` → SubRip → diff against original
  fixture.  Proves the full YouTube-style delivery path.

### Pipeline-stage metadata stamping

Some ANC contents need to surface on the *frame's* `Metadata`,
not just inside the ANC payload (e.g. AFD code →
`Metadata::AspectRatio` on the paired VideoPayload).
Translators and codecs stay pure (AncPacket in, AncPacket out).
A separate pipeline stage `AncMetadataStamper` walks the
assembled Frame, runs registered typed parsers, and stamps
the matching `Metadata` keys.

**Files:**
- [ ] `include/promeki/ancmetadatastamper.h`
- [ ] `src/proav/ancmetadatastamper.cpp`
- [ ] `tests/unit/proav/ancmetadatastamper.cpp`

**Surface:**
- [ ] Each `Stamp` is a `(AncFormat::ID, function<void(const
  AncPacket &, Frame &)>)` pair, registered at static-init
  time via `PROMEKI_REGISTER_ANC_STAMP`.
- [ ] Stage configuration is a `List<AncFormat::ID>` filter
  (empty = run all registered stamps).
- [ ] Default registered stamps:
  - `Afd` → `Metadata::AspectRatio` on the matched VideoPayload
  - `AtcLtc` / `AtcVitc1` / `AtcVitc2` → `Metadata::Timecode`
    on the Frame
  - `HdrStatic2086` → `Metadata::HdrMasteringDisplay` /
    `Metadata::HdrMaxCll` / `Metadata::HdrMaxFall`
  - `Cea708` → `Metadata::HasCaptions = true` boolean

### Future hooks

Documented but not implemented in this phase.

- [ ] **MXF container:** MXF carries ANC as KLV essence
  (SMPTE 436M). Lands when the MXF MediaIO does. New
  transport `MxfKlv` if needed at that time — or absorb
  into `MpegTsPrivate` if KLV semantics are close enough.

(The CEA-708 in NAL SEI codec previously listed here as an
HLS-muxer hook was promoted to a Phase 4 line item — see
"`Cea708 ← / → HlsSei` codec + NVENC SEI injection" above —
because the SEI mechanism applies to any H.264 transport
and is the practical caption delivery path for YouTube /
Twitch / etc.)

---

## Phase 5 — AJA NTV2 SDI capture contract (forward-looking)

No code lands in this devplan for AJA NTV2 — but the ANC
stack ships with the contract the future SDI MediaIO must
satisfy, so that backend is a drop-in producer.

### Documented contract

- [ ] An SDI capture MediaIO that produces ANC must:
  - For every captured frame, scan VANC (and HANC when
    requested) line ranges, build one `AncPayload` listing
    every ST 291 packet found. Each packet uses
    `transport = St291`, `data` set to the RFC 8331 per-packet
    layout (DID, SDID, DataCount, packed UDW, checksum,
    padding), and `meta` populated with Line, HOffset,
    FieldB, CBit, StreamNum.
  - Resolve `AncFormat` via `AncFormat::fromSt291DidSdid(did,
    sdid)`. Unknown DID/SDID pairs use a fallback
    `AncFormat::Invalid` packet (still carried — wire fidelity
    preserved).
  - Set `AncDesc::sourceRaster` and `scanMode` to match the
    paired `ImageDesc` so line numbers are interpretable
    even when the ANC payload is consumed without the video.
  - Stamp `payload.duration` to one frame period of the
    session frame rate (matches the VideoPayload fill
    behavior MediaIO already enforces).
- [ ] **Output side** (SDI sink emitting ANC): the inverse —
  inject the listed ANC packets at the requested line
  numbers, recompute checksums on emit if a translator-built
  packet lacks one, and warn (don't error) when the
  requested line is outside the VBI/VANC region the current
  raster supports.
- [ ] **HDMI capture (future):** the same pattern applies
  with `transport = HdmiInfoFrame` and `meta =
  {HdmiInfoFrame::Type, Version, Length}`. Documented
  alongside SDI in `docs/proav/anc.dox`.

---

## Phase 6 — Inspection, tooling, demos

User-facing surfaces that exercise the stack end-to-end.

### Inspector ANC JSONL dump — landed early (Phase 2b)

The original Phase 6 sketch called for a `mediaio --dump-anc` CLI
flag.  The realish-use-case work in Phase 2b produced an even
better surface: the existing @ref InspectorMediaIO grew a new
`InspectorTest::AncData` test and a `MediaConfig::InspectorAncDataFile`
key, so any pipeline that already terminates in the Inspector
gets per-frame ANC JSONL output for free.  No new CLI tooling
needed:

- [x] `InspectorTest::AncData` enum entry (value 8) + opt-in via
  `MediaConfig::InspectorTests`.
- [x] `MediaConfig::InspectorAncDataFile` (empty = auto-name
  under `Dir::temp()` as
  `promeki_inspector_anc_<pid>_<ns>.jsonl`).
- [x] `InspectorMediaIO::runAncDataCheck` writes one JSON object
  per frame: `{frame, payloadCount, packets:[{format, formatId,
  transport, dataSize, line, fieldB, parsed | hex}]}`.  Parsed
  payloads ride through `AncTranslator::parse`; `Cea708Cdp`
  outputs structured JSON via its `toJson()` method.  Unknown
  formats fall back to a hex dump so the row is self-contained.
- [x] `tests/unit/inspector_ancdata.cpp` — full TPG → Inspector
  pipeline test (1 case / 76 assertions).
- [x] Manual `mediaplay` demo confirmed: CEA-708 caption text
  ("DEMO") goes in via TPG, comes out byte-for-byte in the
  JSONL output.

A standalone `mediaio --dump-anc` CLI is still on the table if a
consumer prefers it over the Inspector route, but the Inspector
surface covers the same use case with zero new tooling code.

### ANC test pattern source

The original sketch called for a single `AncEmission` MediaConfig
key listing the formats to synthesise.  Phase 2b shipped CEA-708
as separate `TpgAncCaptionsEnabled` / (now `TpgAncCaptionsFile`)
/ `TpgAncCaptionsOffset` / `TpgAncCaptionsLine` keys; Phase 3.5c
elevated that to a real SubRip-driven flow.  Functionally
the same per-format approach; the unified `AncEmission` EnumList
key will land when a second format (ATC LTC, AFD, …) joins the
TPG.

- [x] CEA-708 caption injection — landed (Phase 2b extended in
  Phase 3.5c with SubRip file input + per-frame
  `Cea608Encoder` driving the byte stream).
- [ ] Stepped ATC LTC timecode that follows the running frame
  counter.  Trivial — the codec is already registered; just
  needs the TPG-side glue mirroring the CEA-708 path.
- [ ] Cycling AFD code.
- [ ] Fixed-value HDR static metadata (needs Phase 3 codec).
- [ ] Periodic SCTE-104 splice signal (needs Phase 3 codec).
- [ ] Unify under a single `MediaConfig::AncEmission` EnumList
  key (replaces the per-format `TpgAnc*` keys) once 2+ formats
  are wired up — keeps the TPG config surface clean.

### CEA-708 typed (DTVCC) encode/decode — **Full stack landed 2026-05-12**

**Foundation (landed earlier the same day):**

- [x] `Cea708Service` value type modelling a single 708
  service block: `serviceNumber` (1–63), `data` `Buffer`
  (the post-header service-data bytes — C0 / G0 / C1 / G1
  / escape sequences).  Standard (1-byte) header for
  services 1–6 and extended (2-byte) header for services
  7–63 both produced + parsed.  `fromText` / `text` helpers
  for the basic G0 ASCII subset.
- [x] `Cea708DtvccPacket` value type modelling one DTVCC
  packet: `sequenceNumber` (0–3) + `List<Cea708Service>`.
  Full `toPayloadBytes` / `parsePayloadBytes` (handles
  null-block terminator) + `toCcData` / `fromCcData` round-
  trip via `cc_type=2` (DTVCC_PACKET_START) and `cc_type=3`
  (DTVCC_PACKET_DATA) triples.  Header byte packs
  `sequence_number` (2 bits) + `packet_size_code` (6 bits)
  per the spec; odd-byte tails padded with `0xFF`.

**Window state + decoder + encoder (landed):**

- [x] `Cea708WindowState` (`include/promeki/cea708windowstate.h`,
  `src/proav/cea708windowstate.cpp`, 27 unit tests).
  8-window × pen-state × character-buffer state machine.
  Each `Cea708Window` carries `defined` / `visible` flags,
  priority, anchor point / V / H + relative-position flag,
  rowCount × colCount character grid (UTF-32 codepoints
  per cell), pen position, and basic ops (`resize`,
  `clearGrid`, `putChar` with row wrap + roll-up,
  `carriageReturn`, `text` flatten).  `processBytes` /
  `processServiceBytes` walk the service-data stream and
  dispatch:
    - **G0** (0x20..0x7F) including 0x7F → U+266A music note.
    - **G1** (0xA0..0xFF) mapped to Latin-1 supplement
      codepoints.
    - **C0** — NUL, ETX, BS (erases prev char), FF (clears
      window + resets pen), CR (next row + roll-up), HCR
      (clears current row), EXT1 (consumes the right number
      of C2/G2/C3/G3 follow-on bytes — full G2 table is a
      future task, substituted with U+FFFD for now), P16
      (next 2 bytes form a 16-bit codepoint).
    - **C1** — CW0..CW7 (SetCurrentWindow), CLW
      (ClearWindows), DSW (DisplayWindows), HDW
      (HideWindows), TGW (ToggleWindows), DLW
      (DeleteWindows), DLY (Delay), DLC (DelayCancel),
      RST (Reset), SPA (SetPenAttributes), SPC
      (SetPenColor), SPL (SetPenLocation — repositions the
      cursor), SWA (SetWindowAttributes), DF0..DF7
      (DefineWindow — full parameter decode of priority,
      locks, visible, relative-pos, anchor, row/col
      counts + auto-current-window).
  Malformed / truncated bytes never deadlock the parser —
  every branch consumes at least one byte.
- [x] `Cea708Decoder` (`include/promeki/cea708decoder.h`,
  `src/proav/cea708decoder.cpp`, 14 unit tests).
  Pimpl stateful worker (copy/move-deleted).  `pushFrame`
  takes the CDP's `CcDataList`, filters `cc_type=2/3`,
  reassembles DTVCC packets across triples (and across
  frames — packet-data triples can span pushFrame boundaries
  when the packet is large), walks each completed packet's
  service blocks (filtered by configured `serviceNumber`,
  default 1), and feeds the bytes into an internal
  `Cea708WindowState`.  Cue boundaries are recorded on
  visible-text-content transitions: empty→non-empty starts a
  cue (start = current ts), non-empty→empty finalises it
  (end = current ts), non-empty→non-empty(different) closes
  the prior and opens a new one at the same timestamp.
  `finalize` closes any still-displayed cue at the last
  pushFrame ts and returns the accumulated `SubtitleList`.
  `displayedText` / `displayedCue` expose the live cue for
  renderers between pushFrame calls.
- [x] `Cea708Encoder` (`include/promeki/cea708encoder.h`,
  `src/proav/cea708encoder.cpp`, 12 unit tests including 5
  full round-trip cases through `Cea708Decoder`).  Pimpl
  stateful worker, schedule-based (Map<frame, CcDataList>).
  Each cue emits a self-contained two-frame transaction: at
  `startFrame` a packet containing `[DF0(visible, 1-row,
  N-cols), char bytes..., DSW(window 0)]`; at `endFrame` a
  packet containing `[HDW(window 0)]`.  Frames outside cue
  boundaries emit no DTVCC payload.  Overflow (cue text
  longer than the per-packet 127-byte payload cap) surfaces
  `Error::OutOfRange`.

**Generic CaptionEncoder abstraction — landed 2026-05-12:**

- [x] `include/promeki/captionencoder.h` + `src/proav/captionencoder.cpp`:
  abstract @c CaptionEncoder base with virtual @c codec /
  @c frameRate / @c setSubtitles / @c encodableSubset /
  @c nextFrame / @c reset, plus a factory
  @c CaptionEncoder::create(CaptionCodec, Config) returning
  @c UniquePtr<CaptionEncoder>.
- [x] @c Cea608Encoder and @c Cea708Encoder now inherit
  @c CaptionEncoder.  The 608 encoder overrides
  @c encodableSubset with its pre-roll / back-to-back filter;
  the 708 encoder picks up the no-op default.
- [x] @c TpgMediaIO refactored to hold a
  @c List<UniquePtr<CaptionEncoder>> populated from the codec
  selector via the factory; the per-frame loop walks the list
  and concatenates each encoder's @c nextFrame triples into
  the same @c CcDataList.

**Subtitle data-model gaps closed for 708 — landed 2026-05-12:**

- [x] @c CaptionMode TypedEnum (@c Default / @c PopOn /
  @c PaintOn / @c RollUp) in @c enums.h.  @c Subtitle now
  carries @c mode() / @c setMode() so a cue can name its
  display mode explicitly (or stay @c Default to let the
  encoder pick).  Variant wire format + JSON + DataStream
  serialisation all updated.
- [x] @c SubtitleEdgeStyle TypedEnum (@c None / @c Raised /
  @c Depressed / @c Uniform / @c ShadowLeft /
  @c ShadowRight) — mirrors 708 SetPenAttributes
  @c edge_type.
- [x] @c SubtitleOpacity TypedEnum (@c Solid / @c Flash /
  @c Translucent / @c Transparent) — mirrors 708 SetPenColor
  @c fg / bg / edge opacity fields.
- [x] @c SubtitleFontFace TypedEnum (8 face tags from the
  708 spec — @c Default / @c MonoSerif / @c ProportionalSerif
  / @c MonoSans / @c ProportionalSans / @c Casual / @c Cursive
  / @c SmallCaps).
- [x] @c SubtitleSpan extended with @c backgroundColor,
  @c edgeColor, @c edgeStyle, @c fontFace,
  @c foregroundOpacity, @c backgroundOpacity, @c edgeOpacity.
  All seven fields round-trip through Variant / JSON /
  DataStream.  @c SubtitleSpan can now fully express any
  CEA-708 PenAttribute / PenColor styling without information
  loss.

**Wire-level wiring — landed 2026-05-12 (round 2):**

- [x] **Per-list @c CaptionMode dispatch in @c Cea608Encoder.**
  `setSubtitles` walks the input list per-cue, picks up
  each cue's explicit `CaptionMode`, and dispatches to the
  matching per-mode encoder (`encodePopOnCue` /
  `encodePaintOnCue` / `encodeRollUpCue`) sharing a
  `ModeBuilderState` across the whole batch.  Mid-stream
  mode mixing fully supported (landed 2026-05-13): cross-
  mode transitions flush any deferred pop-on / paint-on EDM
  and re-emit RUx on re-entry into roll-up.  Cues with
  @c CaptionMode::Default inherit `Config::mode`.
- [x] @c Cea608Decoder stamps recovered @c CaptionMode on
  every emitted cue (`PopOn` via @c emitDisplayed,
  @c PaintOn / @c RollUp via @c emitLoading driven by the
  decoder's `currentMode` tracker).
- [x] **Per-cue @c CaptionMode dispatch in @c Cea708Encoder.**
  Pop-on (and @c Default) keep the existing
  DefineWindow + chars + DSW at startFrame + HideWindow
  at endFrame transaction.  PaintOn skips the HideWindow
  boundary — the window stays visible after the cue's
  end (real "live" captioning semantics).  RollUp
  declares a multi-row window via the DefineWindow
  `row_count` argument (3 rows) so the receiver scrolls
  instead of overwriting; HideWindow is also skipped.
  Mid-stream mode changes between consecutive cues are
  fully supported — each cue's transaction is self-
  contained.
- [x] @c Cea708Decoder recovers @c CaptionMode from the
  current window state at cue-commit time: row_count > 1
  → @c RollUp, single-row → @c PopOn (the broadcast
  default; pop-on vs paint-on are indistinguishable from
  the wire bytes alone without retaining longer temporal
  context).
- [x] @c SubRip parser/emitter understands the @c &lt;font
  background="..."&gt; libass / Aegisub extension —
  @c SubtitleSpan::backgroundColor round-trips through
  SubRip files.  Reader pushes a parallel bgColorStack
  alongside the existing colorStack; emitter writes both
  attributes on the @c &lt;font&gt; open tag when set.
- [x] @c SubtitleRenderer per-span background paint —
  before each styled run's glyph blits, the renderer fills
  a rectangle covering the run's pixel range with the
  span's @c backgroundColor (when set).  Lands on top of
  the cue's default bg rectangle so a coloured highlight
  reads cleanly over the cue-wide background.
- [x] @c SubtitleRenderer honours @c SubtitleOpacity::Transparent
  for both the foreground (skip the glyph blit) and
  background (skip the bg fill) slots — a span configured
  with @c backgroundOpacity = Transparent reads through to
  whatever is behind it instead of painting a coloured
  rectangle.  Full alpha-blend (@c Translucent / @c Flash)
  is a paint-engine enhancement.

**708 SetPenAttributes / SetPenColor — landed 2026-05-12 (round 3):**

- [x] @c Cea708Encoder emits @c SetPenAttributes (SPA, 0x90)
  and @c SetPenColor (SPC, 0x91) commands before each
  styled span's character bytes.  SPA carries italic /
  underline / edge style / font face; SPC carries fg / bg /
  edge colour quantised to the 2-bit-per-channel wire
  field, plus the fg / bg / edge opacity slots.  Redundant
  emissions are suppressed — only spans whose style
  actually differs from the wire's current pen state
  re-emit the command pair.
- [x] @c Cea708WindowState introduces @c Cea708PenAttr
  (italic / underline / edge style / font face + fg / bg /
  edge colour + opacity) and a @c currentPen() accessor.
  SPA / SPC dispatch in @c processBytes now decodes the
  argument bytes into this field instead of just skipping
  them.
- [x] @c Cea708Decoder reconstructs styled spans on cue
  commit via @c Cea708WindowState::visibleSpans().
  Single-style cues round-trip every styling field
  encoder → decoder.

**708 per-cell pen tracking — landed 2026-05-12 (round 4):**

- [x] @c Cea708Cell struct (codepoint + @c Cea708PenAttr)
  replaces the bare @c uint32_t cell type in
  @c Cea708Window::grid.  @c putChar(cp, pen) writes the
  pen state into the cell so the renderer can recover
  per-character styling.
- [x] @c Cea708Window::visibleSpans() reconstructs the
  window's content as a styled @ref SubtitleSpan list —
  consecutive cells in the same row with matching pen
  state collapse into one span; rows are separated by a
  literal @c "\n" span.  @c Cea708WindowState::visibleSpans()
  walks visible windows in priority order, separating
  windows with another @c "\n" span.
- [x] @c Cea708Decoder snapshots @c visibleSpans() while
  the cue is on screen (the live state is gone by the
  time @c HideWindow flips visibility off and
  @c recordCueBoundaries fires, so a deferred lookup
  would return empty).  Multi-style cues now recover
  with full span boundaries — e.g. a cue of
  `[red italic "RED"] + [green underlined "GRN"]`
  decodes back to those two spans in order.

**608 background colour — landed 2026-05-12 (round 5):**

- [x] @c Cea608 wire helpers: @c encodeBgAttribute,
  @c decodeBgAttribute, @c isBgAttribute for the
  EIA-608-B §7.6 background-attribute code family
  (@c b1=0x10, @c b2 in @c [0x20, 0x2F]).  Maps the
  7-primary @ref CaptionColor palette plus the
  semi-transparent / opaque flag onto the wire bit
  layout.
- [x] @c Cea608Encoder emits a doubled BG attribute
  pair after the PAC when the first span of a row
  declares a background colour, and a fresh doubled
  BG pair mid-row whenever the bg slot changes
  between spans.  Bg colour is quantised through the
  same 608 primary palette as the fg.
  @c SubtitleOpacity::Translucent maps to the
  spec's "semi-transparent" flag; anything else maps
  to opaque.
- [x] @c Cea608Encoder's wrap pass (@c rowSpansFromWords)
  now preserves every styling slot (bg, edge, font,
  opacity) when rebuilding spans during word-wrap —
  previously it dropped everything except the fg
  colour quartet.
- [x] @c Cea608Decoder parses BG attribute codes via a
  new @c doBgAttribute handler and stamps the bg
  colour + opacity onto every span emitted while the
  bg is active.

**Still-pending wire-level follow-ons:**

- [x] Per-cue mid-stream mode mixing in 608 — landed
  2026-05-13.  `Cea608Encoder::setSubtitles` now walks cues
  per-cue (no batch-wide mode coercion), dispatches to the
  matching per-mode encoder via a shared `ModeBuilderState`,
  and emits the mode-establishing control code (RCL / RDC /
  RUx) at every cue's pre-roll.  Cross-mode transitions
  flush any deferred pop-on / paint-on EDM and re-emit RUx
  on re-entry into roll-up.  Per-cue roll-up row count via
  the new `Subtitle::rollUpRows` field — adjacent roll-up
  cues with different counts re-emit the correct RUx.
- [ ] @c SubtitleRenderer edge style + true alpha blending
  for @c Translucent / @c Flash opacities.  Today honours
  fg colour, per-span / per-cue bg rectangles (with
  @c Transparent suppression), italic / bold / underline,
  and the @c Transparent shortcut for fg / bg.  Edge effects
  need a FreeType glyph-outline manipulation pass; partial
  opacity needs a paint-engine alpha-blend primitive.

**TPG 708 emission — landed 2026-05-12:**

- [x] `CaptionCodec` typed enum (`enums.h`) — `Cea608` /
  `Cea708` / `Both`.  Selects which encoder(s) drive the
  per-frame `CcDataList`.
- [x] `MediaConfig::TpgAncCaptionsCodec` (default `Cea608`)
  and `MediaConfig::TpgAncCaptions708Service` (default 1,
  range 1..63) on the TPG config surface.
- [x] `TpgMediaIO` parses the codec key in `executeCmd(Open)`,
  instantiates `Cea608Encoder` / `Cea708Encoder` per the
  selection, and merges both encoders' triples into the same
  CDP on every emitted frame when codec=`Both`.  SCC bypass
  is forced down to 608 (the SCC byte pairs are line-21
  only).
- [x] Encoder holders converted from `std::unique_ptr` to the
  library's `UniquePtr<T>` (`uniqueptr.h`) — library-first
  rule applied to both 608 and the new 708 slot.
- [x] `tests/unit/tpg_anc_captions.cpp` — four new doctests
  cover the 708 path:
    - `codec=Cea708` emits only `cc_type=2/3` triples, no
      608 triples, and `Cea708Decoder` round-trips the cue
      text.
    - `codec=Both` packs the 608 byte pair *and* the 708
      triples into the same CDP at the cue boundaries.
    - `codec=Cea708` with no file emits empty `cc_data` —
      DTVCC has no equivalent of the line-21 null-pair
      filler, so quiet frames carry zero triples.
    - `TpgAncCaptions708Service=2` routes via service 2;
      a service-1 decoder sees nothing, service-2 recovers
      the cue.
- [x] `tests/unit/inspector_ancdata.cpp` — added
  `Inspector: AncData JSONL surfaces CEA-708 DTVCC packets
  from TPG codec=Cea708` covering the full
  TPG → MediaIOPortConnection → Inspector chain.  The JSONL
  shows `cc_type=2` (and optionally 3) triples, no 608
  types, and rebuilding `CcDataList` from the JSON byte
  fields and feeding it through `Cea708Decoder` recovers
  the cue text byte-for-byte.

**Pending follow-ons:**

- [ ] Pen attributes / colour / font preservation on the
  grid.  Currently SPA / SPC / SWA are consumed (correct
  argument counts) but ignored.  Adding them is structural
  (attach a `PenAttr` per grid cell) — no wire-format
  changes needed.
- [x] Full G2 / G3 extended-character tables — landed
  2026-05-13.  New `Cea708Ext` helper (`include/promeki/cea708ext.h`,
  `src/proav/cea708ext.cpp`) carries the CEA-708-D Annex G G2
  table (26 defined positions: smart quotes / em dash / ellipsis /
  trademark / OE / Š / Ÿ / fractions 1⁄8..7⁄8 / box-drawing
  glyphs) plus the lone G3 entry (ATSC CC logo, mapped to U+E000
  in the Private Use Area for round-trip fidelity).  Encoder
  walks codepoints (not bytes) via `String::charAt` and dispatches
  through `Cea708Ext::encode` which picks the cheapest wire
  encoding per codepoint:
  - G0 single byte (`0x20..0x7E`) — ASCII;
  - G0 single byte 0x7F for U+266A (the CEA-708 §7.1.4 music-
    note remap, mirroring the decoder's reverse mapping);
  - G1 single byte (`0xA0..0xFF`) — Latin-1 supplement
    (codepoints `U+00A0..U+00FF`);
  - EXT1 + G2 byte (2 bytes) for codepoints in the G2 table;
  - EXT1 + 0xA0 (2 bytes) for the ATSC CC logo;
  - P16 + hi + lo (3 bytes) for any other BMP codepoint;
  - UTF-16 surrogate pair via two consecutive P16 sequences
    (6 bytes) for astral codepoints (`U+10000..U+10FFFF`).
  Lone surrogate codepoints + codepoints above `U+10FFFF`
  substitute with U+FFFD via P16.  Decoder G2 / G3 lookup
  replaces the prior U+FFFD stub (`Cea708Ext::decodeG2` /
  `decodeG3`); reserved table positions still fall back to
  U+FFFD.  `Cea708WindowState` now holds a
  `_pendingHighSurrogate` slot so a UTF-16 surrogate pair
  survives a `processBytes` boundary (e.g. across DTVCC
  packets); orphaned high surrogates decay to U+FFFD when
  any non-P16 byte arrives in between.  Test coverage:
  `tests/unit/cea708ext.cpp` (16 cases covering G2 / G3
  round-trip + the composite `encode` decision tree),
  6 new cases in `cea708windowstate.cpp` (G2 mapped vs
  reserved fall-back, G3 ATSC logo, P16 surrogate-pair
  pairing both within a single `processBytes` call and across
  the call boundary, orphaned-surrogate decay), 7 new cases
  in `cea708encoder.cpp` (G1 / G2 / OE / box-drawing /
  Korean BMP / astral G clef / mixed-encoding cue
  round-trip).
- [ ] Paint-on / roll-up 708 modes.  Today the encoder
  emits the pop-on style "Define + write + Display"
  transaction; a real paint-on flow would have the window
  already displayed and stream chars live without a
  Define/Display boundary.  Decoder already handles either
  shape (it's wire-state-driven).
- [ ] `Cea708Service` Variant X-macro integration.  Currently
  consumed by direct API.  One-line addition when an
  application needs it.
- [x] `SubtitleSource::Cea708Anc` enum value + handler in
  `SubtitleBurnMediaIO` — landed 2026-05-12.  Mirrors the
  608 wiring: a `UniquePtr<Cea708Decoder>` lifted on
  open() when the source is listed, `tryCea708AncSource()`
  walks each frame's `AncPayloads`, filters Cea708 packets,
  parses the CDP via the shared `AncTranslator`, and pushes
  each `cc_data` triple list into the decoder; the renderer
  paints `Cea708Decoder::displayedCue()`.  Coexists with
  `Cea608Anc` — both decoders can run in parallel and the
  ordered source preference picks the winner.  Tests:
  `SubtitleBurnMediaIO[708]: Cea708Anc source paints a cue
  from a real CDP` synthesises a CDP via `Cea708Encoder` and
  verifies `FramesPainted` advances; a negative-case test
  confirms empty `cc_data` leaves `FramesPainted` at 0.

### Caption renderer — **Phase 1 landed 2026-05-11**

The first cut of the visible caption-rendering surface landed
early because it is independent of the CEA-708 DTVCC decoder
work — anywhere that produces a @ref Subtitle (TPG's
`TpgAncCaptionsFile`, the Phase 3.5 SubRip parser, a future
NDI / RTMP subtitle source) now has a paint path.

**Surface that landed:**

- `SubtitleSpan` value type carrying `(text, bold, italic,
  underline, color)` plus a `SubtitleSpan::List` alias.
  Variant tag `TypeSubtitleSpan` = `0x5D`.  Default colour
  invalid = "inherit from renderer default".
- `Subtitle` extended with `spans()` / `setSpans()` and a
  full-style constructor.  The legacy text-only constructors
  still work and synthesise a single unstyled span.  The
  `text()` accessor returns the cached concatenation of all
  spans' text so existing consumers keep working.  Wire
  format is the field set + `List<SubtitleSpan>` (the redundant
  `text` field was dropped per the no-backwards-compat
  preference).
- `SubRip::parse` / `emit` now lex inline markup (`<i>`,
  `<b>`, `<u>`, `<font color>`, ASS-style aliases `<em>`,
  `<strong>`) into structured spans, capture WebVTT-style
  `<v Speaker>...</v>` into @ref Subtitle::speaker, and
  re-emit canonical markup from spans.  Round-trip through
  the etc/substest.srt fixture is byte-stable.
- `FastFont` rekeyed its glyph cache from `uint32_t codepoint`
  to `(codepoint, fg, bg, styleFlags)`.  Setting the
  foreground / background colour no longer drops the cache —
  a new `Font::onColorChanged` hook splits colour switches
  off from `onStateChanged`.  Per-call `FastFont::DrawStyle`
  override carries an explicit fg / bg / bold / italic /
  underline payload.  Bold / italic go through
  `FT_GlyphSlot_Embolden` / `FT_GlyphSlot_Oblique`; underline
  is drawn as a separate rectangle after the glyph blits
  using the face's `underline_position` / `underline_thickness`.
- `SubtitleRenderer` class (header-only-friendly,
  pimpl-less) — holds a `FastFont`, manages anchor-driven
  layout, optional background box, multi-line stacking with
  `\n`, multi-span per line.  Honours `Subtitle::anchor`,
  `Subtitle::region`, and (via `setTopReserved` /
  `setBottomReserved`) reserved vertical bands at the frame
  edges.  `setAnchorOverride(SubtitleAnchor::Default)` falls
  through to the cue's own anchor (which itself falls back to
  `BottomCenter` when `Default`).
- `SubtitleBurnMediaIO` factory `"SubtitleBurn"` (Transform
  mode) walks an ordered `VideoSubtitleBurnSources`
  `EnumList<SubtitleSource>` preference list and paints the
  first non-empty cue it finds.  Sources currently
  available:
    - `SubtitleSource::Metadata` — `Metadata::Subtitle` on
      the frame (TPG-stamped or any future producer).
    - `SubtitleSource::Cea608Anc` — CEA-608 decoded from
      `ancPayloads()` via a held `Cea608Decoder` +
      `AncTranslator` pair.  The decoder is fed every
      frame's `Cea708Cdp::CcDataList` and queried via the
      new `Cea608Decoder::displayedCue()` accessor which
      returns the live styled cue (anchor + spans), not
      just flat text.
  Future sources (`Cea708Anc` for DTVCC, `HlsSei`, `RtmpAmf`,
  `NdiXml`) slot in by extending the enum and adding a
  matching handler.  Same back-pressure / capacity / paint-
  engine-gating story as `BurnMediaIO`.
- Config keys: `VideoSubtitleBurnEnabled`,
  `VideoSubtitleBurnFontPath`, `VideoSubtitleBurnFontSize`,
  `VideoSubtitleBurnTextColor`, `VideoSubtitleBurnBgColor`,
  `VideoSubtitleBurnDrawBg`, `VideoSubtitleBurnAnchor`,
  `VideoSubtitleBurnSources` (default `[Metadata]`), plus
  the existing `Capacity`.  The earlier
  `VideoSubtitleBurnDecodeAnc` bool was rolled into the
  `Sources` list on 2026-05-11 so source priority is
  user-controlled instead of hard-coded.
- `Color::nearestPaletteIndex(const Color *palette, size_t n)`
  + `Color::List` convenience overloads — sRGB Euclidean
  palette match.  Used by the 608 encoder's colour
  quantisation; also useful for TUI 16-/256-colour
  downscale and indexed image formats.

**What it does not do (yet):**

- [ ] CEA-708 DTVCC service-block decode — still pending the
  Phase 6 `Cea708Service` / `Cea708WindowState` typed
  decoder.  When that lands, a new
  `SubtitleSource::Cea708Anc` enum value slots into the
  `VideoSubtitleBurnSources` list alongside `Cea608Anc`;
  the `SubtitleRenderer` itself is unaffected — it already
  consumes the format-agnostic `Subtitle`.
- [ ] Inline-tag escape in the SubRip emitter (a span's text
  that contains a literal `<X>` could collide with markup).
  Real-world cue text essentially never carries `<` so this
  is more of a hygiene cleanup than a correctness gap.
- [ ] Translucent backgrounds for the cue box.  Today's
  background draw is opaque-only; a future config key
  (`VideoSubtitleBurnBgOpacity`) can take a 0..1 alpha and
  thread through to `Color::setAlpha`.

The original optional `Cea708Overlay` line item below now
collapses into this section — the same `SubtitleBurnMediaIO`
serves the role once the DTVCC decode work fills in the
remaining ANC source path.

### promeki-test functional matrix

- [ ] `tests/func/anc-rtp40-roundtrip/` — TPG (with CEA-708
  captions) → RTP-40 → receiver → byte-exact ANC compare.
  All the moving parts are landed; this is a wiring task.
- [ ] `tests/func/anc-ndi-roundtrip/` — needs the Phase 4
  NDI ANC codec.
- [ ] `tests/func/anc-rtmp-captions/` — needs the Phase 4
  RTMP AMF CEA-708 codec.
- [ ] `tests/func/anc-mediapipeline-passthrough/` — ANC enters
  via one MediaIO, leaves via another with the same wire
  format, verifies byte-exact passthrough (no translation).
  Doable today on the (TPG-emit → Inspector-receive) leg; a
  passthrough sink (writing the same wire form back out)
  needs a small MediaIO addition.
- [ ] `tests/func/anc-cross-transport/` — ANC enters as
  St291, leaves as NdiXml or RtmpAmf, verifies semantic
  equivalence (typed parse on both ends matches).  Needs
  Phase 4 NDI / RTMP ANC codecs.

### Documentation

- [ ] `docs/proav/anc.dox` — top-level chapter covering the
  generic packet, the descriptor, the two registries, the
  transport helpers, and the SDI / HDMI / MPEG-TS capture
  contracts. Worked example showing how to receive ANC via
  RTP-40 and re-emit it on NDI.
- [ ] Update `docs/proav/mediaio.dox` to reference ANC
  alongside video/audio.
- [ ] Add a CEA-708 captions walkthrough that points at the
  Inspector AncData JSONL output as the canonical way to
  verify caption flow through a pipeline.

---

## Open questions / things to revisit during build

### Settled during Phase 0–2b

- **~~Should `RtpMediaIO` carry data + anc on the same
  m=section or on two?~~** **Settled (Phase 1):** one m=section.
  The existing data m=section becomes the ANC section when
  `DataRtpFormat::St2110_40` is selected.  Reversible if a
  deployment ever needs simultaneous JSON + RFC 8331.
- **~~Checksum recompute on translate~~** — settled:
  governed by `AncTranslateConfig::Checksum`
  (`AncChecksumPolicy`).  Phase 2 codecs use the
  `St291Packet::build` path which always recomputes; the
  three-way policy matrix is wired but the per-codec doctest
  matrix is still pending (no codec yet emits a packet with
  a caller-provided checksum that differs from the
  recomputed one).

### Still open

- **Multi-packet CDP (CEA-708 > 255 bytes).**  SMPTE 334-2 §6.3
  splits oversize CDPs across multiple ST 291 packets sharing a
  sequence counter.  The current codec returns
  `Error::OutOfRange` for CDPs > 255 bytes.  Wire when a real
  source emits a multi-packet CDP, or when caption-data density
  rises above the per-frame cap (typically a non-issue at modern
  frame rates with 608-only payload, but 708 service blocks can
  push it).
- **Per-line-number injection on sinks:** SDI emit ordering
  matters (some receivers care about VANC line monotonicity).
  Spec it in the SDI contract section but don't bake
  enforcement into `AncPayload` itself — leave that to the
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
  KLV in transport-stream private sections need a TS
  demuxer to surface to the AncPayload. The transport enum
  and format IDs ship now; the demuxer is a separate
  project's responsibility.
- **~~CEA-608 styled captioning beyond pop-on plain text~~**
  — settled 2026-05-12.  The encoder + decoder pair now
  carries:
    - **All three CEA-608 modes** — pop-on (Phase 3.5a/b),
      paint-on (`RDC` → PAC → live chars → EDM), and
      roll-up (`RU2/3/4` once + `CR` + PAC + chars per
      cue, no per-cue EDM).  Mode is established by the
      first control code seen; the decoder tracks it in
      `currentMode` and finalises cues appropriately
      (EDM in paint-on, CR in roll-up, EOC + EDM in
      pop-on).
    - **Row (1..15)** mapped from `SubtitleAnchor` on encode
      (Top* → 1, Middle* → 8, Bottom* → 15) and recovered
      to the centred variant per row group on decode.
      Roll-up forces row 15 regardless of cue anchor.
    - **Foreground colour** quantised to the 7-primary
      palette (white / green / blue / cyan / red / yellow /
      magenta) via `Color::nearestPaletteIndex`.  PAC
      carries the colour at line start; per-span colour
      changes within the cue emit a doubled mid-row code.
    - **Italic + underline** flags via PAC at line start
      and via mid-row codes mid-cue.  Italic on the 608
      wire always pairs with white per spec.
    - **Bold** is not representable in 608.  Encoder emits
      a one-shot warning per `setSubtitles` and drops the
      flag from the wire bytes.
  **Horizontal positioning landed 2026-05-13.**  Encoder
  walks the cue's @ref SubtitleAnchor horizontal half (Left /
  Center / Right): @c BottomLeft + @c Default land flush-left
  at column 0, @c BottomCenter at @c (32-rowWidth)/2, @c BottomRight
  at @c 32-rowWidth.  PAC indent slot (multiples of 4) carries
  the bulk; doubled Tab Offset (T1/T2/T3 = @c (0x17, 0x21..0x23)
  on CC1, @c (0x1F, …) on CC2) carries the 1..3-column
  residual.  PAC's 4-bit subfield carries colour OR italic OR
  indent (mutually exclusive on a single PAC), so a coloured
  cue at @c BottomCenter degrades back to flush-left column 0
  — colour wins because it's the more prominent visual cue,
  and SubRip files in the wild rarely combine both.  Decoder
  tracks the row's start column (PAC.indentCol +
  cumulative Tab Offset shift) and recovers Left / Center /
  Right via column thresholds (@c column < 4 → Left,
  @c < 24 → Center, else → Right) joined with the existing
  vertical row-group heuristic.  Tests:
  @c "BottomCenter anchor with 4-char cue uses PAC indent +
  Tab Offset", @c "BottomRight anchor with 6-char cue lands
  flush-right", @c "BottomCenter / BottomRight / BottomLeft
  anchor round-trips horizontal half" plus
  @c "TopCenter and MiddleCenter anchors round-trip" and the
  coloured-cue degradation test.

  **CEA-608 extended characters landed 2026-05-13.**  New
  `Cea608Ext` helper (`include/promeki/cea608ext.h`,
  `src/proav/cea608ext.cpp`) carries all three extended-
  character tables:
    - The 10 remapped basic-G0 positions
      (0x2A=á / 0x5C=é / 0x5E=í / 0x5F=ó / 0x60=ú / 0x7B=ç /
      0x7C=÷ / 0x7D=Ñ / 0x7E=ñ / 0x7F=█).
    - The 16 Special Characters via doubled @c (0x11/0x19,
      0x30..0x3F) (® / ° / ½ / ¿ / ™ / ¢ / £ / ♪ / à / NBSP /
      è / â / ê / î / ô / û).
    - The 64 Extended Western European glyphs across two
      pairs: doubled @c (0x12/0x1A, 0x20..0x3F) (Spanish / French /
      misc) and doubled @c (0x13/0x1B, 0x20..0x3F)
      (Portuguese / German / Danish / box-drawing).
  Encoder walks codepoints (not bytes) via @c String::charAt
  and routes each through @c Cea608Ext::encode to pick the
  cheapest path: BasicG0 (one wire byte), Special / ExtSpanish /
  ExtFrench (one placeholder G0 byte for old-decoder fallback,
  followed by a doubled control pair that triggers the
  receiver's "replace previously displayed character"
  semantics).  When the placeholder lands alone in its char
  pair (no neighbour to share with), the encoder pads the
  pair's second byte with @c NUL (0x00) instead of @c 0x20
  space — receivers ignore @c NUL after parity strip, so the
  cursor doesn't advance past the placeholder before the
  doubled control code lands.  Decoder dispatches the three
  control-code families ((b1 & 0xF7) == 0x11 / 0x12 / 0x13)
  via a new @c replaceLastWithCodepoint helper that pops the
  most recent codepoint from the loading buffer and appends
  the mapped glyph (handles the live PaintOn / RollUp
  displayedFlat mirror too).  Test coverage:
  @c tests/unit/cea608ext.cpp (10 cases on the table itself —
  basic-G0 ASCII passthrough, the 10 remapped positions,
  16 Special / 32 + 32 Extended round-trip, placeholder
  sanity, no-mapping fall-through), 8 new round-trip cases in
  @c cea608decoder.cpp covering basic-G0 remap, Special
  Character (™), mid-cue Specials (½, ¿), lone leading
  Special with NUL-pad path (♪), Extended Spanish / French,
  no-mapping codepoint substitution, mixed-encoding cue.
- **CEA-708 service-block decode.**  Same story as CEA-608 but
  for the DTVCC side (cc_type=2/3 triples).  As of 2026-05-12
  the foundation is in place: `Cea708Service` (one service
  block) + `Cea708DtvccPacket` (sequence + service-block
  list, full `toCcData` / `fromCcData` round-trip + null-
  block terminator + standard / extended header forms).
  The remaining piece is `Cea708WindowState` — the
  8-window × pen-state × character-buffer state machine
  that flattens service blocks into a `SubRip` cue.  Out
  of scope for the wire-layer codec; a Phase 6 "caption
  renderer" task.
- **`TpgAncCaptionsEnabled` with no video.**  A TPG configured
  with only ANC captions and no video / audio / timecode
  currently still passes open() and emits Frames with just
  an `AncPayload`.  Whether downstream pipelines (planner,
  port connection, sinks) cope with a video-less Frame is
  not yet exercised end-to-end — the Inspector test
  pipelines always have video.  Probably fine but worth a
  smoke test before promoting to a documented use case.
- **~~Subtitle file → Cea608Encoder pre-roll error reporting~~**
  — settled 2026-05-11.  `Cea608Encoder::encodableSubset`
  pre-flights the cue list and returns the encodable subset
  plus an optional dropped-cue list.  `TpgMediaIO` runs the
  filter and logs a per-cue warning (with the cue's time
  range and text) for each drop, so a tightly-packed `.srt`
  no longer fails open — the listener just sees the warnings
  and the rest of the captions still flow.
- **TPG frame-rate code mapping is closed-form.**  Anything
  not in `{23.976, 24, 25, 29.97, 30, 50, 59.94, 60}` emits
  `frameRateCode = 0` (unknown).  Round-trip is still
  structural; widen the table when a non-standard rate
  ships.

### Tiny library follow-ups surfaced by Phase 2b

- **`String::reserve(size_t)` missing.**  The class only
  exposes `reverse()` (palindrome-style).  A standard
  `reserve` is a one-line addition to `string.h` and would
  let callers pre-size hex-dump buffers etc. cheaply.
- **`File::remove(path)` missing.**  The Inspector AncData
  test falls back to `std::remove(path.cstr())`.  A static
  `File::remove(const String &)` would be a clean addition.
- **`Variant::get<T>()` for non-Variant T fails at link
  time.**  Calling `Variant::get<JsonObject>()` (JsonObject
  is not in the Variant X-macro) produces a long mangled
  link error instead of a clear compile-time message.  A
  `static_assert(detail::is_variant_type_v<T>, "T is not a
  registered Variant payload type")` inside `VariantImpl::get`
  would fail loudly at the call site.
- **`TypedEnum` not a Variant payload type — only base
  `Enum` is.**  Calling `cfg.getAs<AncFidelity>` fails to
  link; the established pattern is `cfg.get(key).asEnum(
  AncFidelity::Type).value() == AncFidelity::Full.value()`.
  Verbose; consider whether `TypedEnum<Derived>` should
  grow a Variant payload specialisation that round-trips
  through the base `Enum` type.
