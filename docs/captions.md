# Closed Captions (CEA-608 / CEA-708) {#captions}

End-to-end captioning surface: how subtitle text becomes wire-format
caption packets, how packets are decoded back into structured cues,
and how the typical pipelines wire it together.  Covers both
CEA-608 (line-21 byte pairs) and CEA-708 (DTVCC service blocks).

## Overview {#captions_overview}

libpromeki carries closed-caption data as @ref promeki::AncPayload
packets riding on @ref promeki::Frame.  The wire layer is the SMPTE
334-2 Caption Distribution Packet (@ref promeki::Cea708Cdp) which
holds a list of cc_data triples; each triple's `cc_type` field
selects between CEA-608 (line-21 field 1 / 2) and CEA-708 (DTVCC
packet start / continuation).

The library's caption stack has three layers:

- **Wire layer.**  @ref promeki::Cea708Cdp serialises / parses the
  CDP byte form (riding through the @ref promeki::AncTranslator on
  ST 291 / RTP / HLS-SEI / RTMP-AMF / NDI XML transports).
- **Codec layer.**  @ref promeki::Cea608Encoder /
  @ref promeki::Cea608Decoder and @ref promeki::Cea708Encoder /
  @ref promeki::Cea708Decoder convert between a
  @ref promeki::SubtitleList timeline and per-frame cc_data triples.
- **Application layer.**  @ref promeki::SubRip parses / emits
  `.srt` files; @ref promeki::SubtitleRenderer rasterises a
  @ref promeki::Subtitle onto a frame; @ref promeki::SubtitleBurnMediaIO
  is the pipeline stage that pulls captions out of frame ANC and
  paints them into the picture for QC.

## Quick start {#captions_quickstart}

The fastest way to drive a stream with captions through the test
pattern generator and verify the recovered cues:

```cpp
#include <promeki/mediaio.h>
#include <promeki/mediaiofactory.h>
#include <promeki/mediaconfig.h>

MediaIOConfig cfg = MediaIOFactory::defaultConfig("TPG");
cfg.set(MediaConfig::TpgAncCaptionsEnabled, true);
cfg.set(MediaConfig::TpgAncCaptionsFile, "etc/substest.srt");

// Cea608, Cea708, or Both — the default is Cea608.
cfg.set(MediaConfig::TpgAncCaptionsCodec, CaptionCodec(CaptionCodec::Cea708));

MediaIO *src = MediaIO::create(cfg);
```

The TPG loads the SRT at @c open() time, schedules each cue through
the in-process @ref promeki::Cea708Encoder, and merges the per-frame
cc_data triples into the CDP carried on every emitted
@ref promeki::Frame's @ref promeki::AncPayload list.

To recover captions on the receive side:

```cpp
#include <promeki/cea708decoder.h>

Cea708Decoder dec;
for (const Frame &frame : frames) {
    for (const AncPayload &payload : frame.ancPayloads()) {
        // Walk every ANC packet whose format is Cea708 (CDP).
        for (const AncPacket &pkt : payload.packets()) {
            if (pkt.format() != AncFormat::Cea708) continue;
            Result<Cea708Cdp> r = Cea708Cdp::fromBuffer(pkt.data());
            if (r.second().isError()) continue;
            dec.pushFrame(frame.videoFrameNumber(), frame.timeStamp(),
                          r.first().ccData);
        }
    }
}
SubtitleList recovered = dec.finalize();
```

## CEA-708 (DTVCC) {#captions_cea708}

CEA-708 is the digital television closed-caption format.  It rides
in the CDP's cc_data section as `cc_type=2` (DTVCC_PACKET_START) and
`cc_type=3` (DTVCC_PACKET_DATA) triples that pack into
@ref promeki::Cea708DtvccPacket objects.  Each packet carries one or
more @ref promeki::Cea708Service blocks (services 1..63, each
identifying a separate caption language stream).

### Encoder {#captions_cea708_encoder}

@ref promeki::Cea708Encoder consumes a @ref promeki::SubtitleList
and produces per-frame @ref promeki::Cea708Cdp::CcDataList
schedules.  Each cue emits a transaction:

```
DefineWindow(window 0, invisible, anchor + rowCount + colCount)
SetWindowAttributes (optional — when cue has a uniform bg colour)
SetPenAttributes (default — italic/underline/edge off, default font)
SetPenColor (default — opaque white fg, transparent bg)
[per-span SPA / SPC re-asserted on style transitions]
character bytes (via G0 / G1 / G2 / G3 / P16 / UTF-16 surrogate pair)
DisplayWindow(window 0)
```

For @ref promeki::CaptionMode::PopOn (the default), a
HideWindow packet rides at the cue's @c end timestamp so the cue
clears.  @ref promeki::CaptionMode::PaintOn and
@ref promeki::CaptionMode::RollUp skip the HideWindow — the
visible window stays defined until the next cue's DefineWindow
replaces it.

Per-cue mode mixing is supported: cue 1 can be PopOn, cue 2 can be
RollUp, cue 3 can be PaintOn.  Each cue's transaction is
self-contained.

The encoder respects the SMPTE 334-2 / CEA-708 5-bit `cc_count`
field — at most 31 triples per CDP per video frame.  When a cue's
show transaction needs more triples than fit in one frame, packets
are scheduled across consecutive frames starting at the cue's
@c startFrame.  A cue too long to fit before its @c endFrame is
dropped with a warning.  When the @c HideWindow of one cue would
collide with the @c DefineWindow of the next cue at the same
frame, the @c HideWindow is elided (the next cue's
DefineWindow + DisplayWindow atomically replaces the visible
window).

#### Character encoding

The encoder picks the cheapest wire encoding per Unicode codepoint
via @ref promeki::Cea708Ext::encode:

| Codepoint | Wire | Cost |
|----|----|----|
| `U+0020..U+007E` | G0 (one byte) | 1 byte |
| `U+266A` (♪) | G0 0x7F | 1 byte |
| `U+00A0..U+00FF` | G1 single byte | 1 byte |
| CEA-708-D Annex G G2 mapped | `EXT1 + G2 byte` | 2 bytes |
| ATSC CC logo | `EXT1 + 0xA0` | 2 bytes |
| Other BMP codepoint | `P16 + hi + lo` | 3 bytes |
| Astral (`U+10000..U+10FFFF`) | UTF-16 surrogate pair via two P16 sequences | 6 bytes |

Lone surrogate codepoints and codepoints above `U+10FFFF`
substitute with U+FFFD via P16.

### Decoder {#captions_cea708_decoder}

@ref promeki::Cea708Decoder filters the configured service
(default 1), reassembles DTVCC packets across frames (a single
DTVCC packet can span multiple `cc_type=3` continuation triples
across multiple video frames), and feeds the service data through
@ref promeki::Cea708WindowState — an 8-window grid + pen state
state machine that consumes the CEA-708-D byte stream.

Cue boundaries are emitted on visible-text transitions inside the
window state:

- empty → non-empty starts a cue at the current timestamp;
- non-empty → empty finalises the cue;
- non-empty → non-empty (different text) closes the prior cue and
  opens a new one at the same timestamp.

@ref promeki::Cea708Decoder::displayedCue exposes the live cue
between @ref promeki::Cea708Decoder::pushFrame calls — useful for
the caption renderer in @ref promeki::SubtitleBurnMediaIO.

### Service multiplexing

A single CDP can carry multiple services (one window-state set per
service number).  Configure
@ref promeki::Cea708Decoder::Config::serviceNumber to pick a
specific service; the encoder side picks via
@ref promeki::Cea708Encoder::Config::serviceNumber or the
@ref promeki::MediaConfig::TpgAncCaptions708Service config key.

### Styling

CEA-708 carries the full per-character style stack: italic /
underline / edge style / font face (via SetPenAttributes) and
foreground / background / edge colour + opacity (via
SetPenColor).  @ref promeki::Cea708Window::visibleSpans
reconstructs styled @ref promeki::SubtitleSpan runs from the
window's grid; consecutive cells with the same pen state collapse
into one span.

Window-level styling (uniform background colour, border, justify,
print / scroll direction, word-wrap, display effect) rides through
@ref promeki::Cea708WindowAttr via SetWindowAttributes.  Spans
without an explicit per-cell SPC bg inherit the SWA fill; per-cell
SPC bg always wins where set.

Caveats:

- Colours are 2-bit-per-channel on the wire (4 levels per
  channel).  @ref promeki::SubtitleSpan colours are quantised
  through the closest 4-level palette match on encode and
  expanded back to 8-bit via the canonical `{0, 85, 170, 255}`
  table on decode.  A pure-red span round-trips byte-exact; an
  off-palette colour will quantise to its nearest 64-tone bucket.
- Pop-on vs paint-on are indistinguishable from the wire bytes
  alone without retaining longer temporal context — the decoder
  reports the broadcast default (PopOn) for single-row windows
  and RollUp for multi-row windows.

## CEA-608 {#captions_cea608}

CEA-608 is the analog NTSC line-21 closed-caption format.  It
rides in the CDP's cc_data section as `cc_type=0` (field 1) and
`cc_type=1` (field 2) triples.  Each triple's `b1`/`b2` field
carries a two-byte caption-data symbol that's interpreted by a
state machine modelled in @ref promeki::Cea608Encoder /
@ref promeki::Cea608Decoder.

### Encoder

@ref promeki::Cea608Encoder supports all three CEA-608 modes:

- @ref promeki::CaptionMode::PopOn — RCL → PAC → chars → EOC →
  EDM transaction.
- @ref promeki::CaptionMode::PaintOn — RDC → PAC → chars (live)
  → EDM transaction at cue end.
- @ref promeki::CaptionMode::RollUp — RUx (2/3/4 rows) → CR →
  PAC → chars per cue, no per-cue EDM.

Mid-stream mode mixing is fully supported.

Per-cue features:

- Row 1..15 mapped from @ref promeki::SubtitleAnchor's vertical
  band (Top* → 1, Middle* → 8, Bottom* → 15).  Roll-up forces
  row 15 regardless of cue anchor.
- Horizontal positioning via PAC indent slot (multiples of 4) +
  doubled Tab Offset T1/T2/T3 for the 1..3-column residual.
  Coloured cues degrade to flush-left column 0 — PAC's 4-bit
  subfield carries colour OR italic OR indent (mutually
  exclusive per spec).
- Foreground colour quantised to the 7-primary palette
  (white / green / blue / cyan / red / yellow / magenta) via
  @ref promeki::Color::nearestPaletteIndex.
- Background colour (8 wire values including semi-transparent
  black) via doubled BG attribute codes.
- Italic + underline flags via PAC at line start, doubled mid-row
  codes mid-cue.
- Bold isn't representable in 608 — the encoder warns once per
  @ref promeki::Cea608Encoder::setSubtitles and drops the flag.
- Cross-channel mixing: CC1..CC4 in field 1 / 2.

Extended character coverage (Spanish / French / German / Portuguese /
Danish + smart quotes / fractions / symbols) lands via
@ref promeki::Cea608Ext: the 10 remapped basic-G0 positions, 16
Special Characters via doubled `(0x11/0x19, 0x30..0x3F)`, and
64 Extended Western European glyphs via doubled `(0x12/0x1A,
0x20..0x3F)` + `(0x13/0x1B, 0x20..0x3F)`.

### Decoder

@ref promeki::Cea608Decoder dispatches on the wire byte pairs
(parity-stripped) and maintains a per-channel mode state.  Cue
boundaries are emitted on EOC (pop-on commit), EDM (paint-on
flush), and CR (roll-up advance).  Recovered cues carry the
detected @ref promeki::CaptionMode, row group, foreground colour,
italic / underline flags, anchor reconstructed from PAC indent +
Tab Offset column tracking, and styled @ref promeki::SubtitleSpan
runs.

## TPG injection {#captions_tpg}

The test pattern generator can be configured to inject captions
from a SubRip `.srt` file via the
@ref promeki::MediaConfig::TpgAncCaptionsFile key (or directly from
a Scenarist SCC `.scc` file via
@ref promeki::MediaConfig::TpgAncCaptionsScc for raw-bytes bypass
testing).

The codec is selectable via
@ref promeki::MediaConfig::TpgAncCaptionsCodec:

- `CaptionCodec::Cea608` (default) — line-21 byte pairs only.
- `CaptionCodec::Cea708` — DTVCC service blocks only.  Service
  number is configurable via
  @ref promeki::MediaConfig::TpgAncCaptions708Service.
- `CaptionCodec::Both` — both encoders run side-by-side; the
  emitted CDP carries both wire forms (mirrors real SDI broadcast
  practice).

When a cue can't be encoded (e.g. too tight a frame window for a
long cue), the TPG logs a warning and continues — the rest of the
captions still flow.

## Inspection / verification {#captions_inspection}

@ref promeki::InspectorMediaIO with
@ref promeki::InspectorTest::AncData enabled dumps every emitted
frame's ANC packet array as JSON Lines to
@ref promeki::MediaConfig::InspectorAncDataFile.  Each row's
`packets[].parsed.ccData` array holds the per-frame cc_data
triples in `{valid, type, b1, b2}` form.

The two functional cases under @c utils/promeki-test/cases/captions.cpp
exercise the full chain end-to-end:

- `captions.cea608.subrip_roundtrip` — TPG (608) → Inspector
  AncData JSONL → @ref promeki::Cea608Decoder →
  @ref promeki::SubRip::emit → cue-by-cue comparison.
- `captions.cea708.subrip_roundtrip` — same shape, 708 codec.

Run via:

```
promeki-test -k "captions"
```

The recovered SRT lands in the per-test scratch folder at
`roundtrip.srt` for post-mortem diffing.

## Burn-in for QC {#captions_burn_in}

@ref promeki::SubtitleBurnMediaIO is a `Transform`-mode MediaIO
that pulls captions out of every frame and paints them into the
picture using @ref promeki::SubtitleRenderer.  Source preference
order is configurable via
@ref promeki::MediaConfig::VideoSubtitleBurnSources
(@ref promeki::SubtitleSource enum list):

- `SubtitleSource::Metadata` — `Metadata::Subtitle` field on the
  frame (TPG-stamped or any future producer).
- `SubtitleSource::Cea608Anc` — CEA-608 decoded from
  `ancPayloads()` via an internal @ref promeki::Cea608Decoder.
- `SubtitleSource::Cea708Anc` — CEA-708 DTVCC decoded via an
  internal @ref promeki::Cea708Decoder.

The first non-empty cue in source preference order paints.  Same
back-pressure / capacity / paint-engine-gating story as the
generic @c BurnMediaIO.
