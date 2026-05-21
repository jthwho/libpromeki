# Timecode {#timecode}

SMPTE timecode and its high-frame-rate extensions: ST 12-1 LTC,
ST 12-2 ATC carriage, ST 12-3 HFR.

The timecode subsystem is the home of @ref promeki::Timecode,
@ref promeki::TimecodeUserbits, the LTC encoder / decoder, the
@ref promeki::TimecodeGenerator, and the codecs that emit and
parse timecode on the ATC ancillary-data carriage.  It delegates
the bit-level math to the vendored `libvtc` C library and layers
the libpromeki value-type API, DataStream / Variant / JSON
serialisation, and the typed ATC codecs on top.

## Physical-frame semantics {#timecode_physical_frame}

@ref promeki::Timecode is **always physical-frame**.  At 60p the
`frame()` digit walks 0..59 across one wall-clock second; at 120p
it walks 0..119; at the three ST-defined drop-frame rates
(29.97 DF, 59.94 DF, 119.88 DF) it follows the per-minute drop
pattern scaled by the high-frame-rate multiplier.

This is a deliberate departure from the libvtc-native "super-frame
+ sub-frame" split.  At HFR the wire carriage (ATC_VITC1/VITC2
under ST 12-2 or ATC_HFRTC under ST 12-3) does encode super-frame
digits and sub-frame identifier bits separately, but that
split lives below the @ref promeki::Timecode API.  Application
code increments `Timecode` one physical frame at a time; the codecs
do the super-frame / pair-index / sub-frame conversion on the
emit and parse sides.

The two helpers @ref promeki::Timecode::superFrameIndex and
@ref promeki::Timecode::subFrameIndex expose the split when a
codec or test needs to reason about it directly.  At non-HFR rates
they return the natural fallback (`frame()` and `0`, respectively)
so call sites can be rate-agnostic.

| Accessor                                                | Non-HFR (≤30 fps)     | HFR (>30 fps)                                  |
|---------------------------------------------------------|-----------------------|------------------------------------------------|
| @ref promeki::Timecode::frame                           | 0..fps-1              | 0..fps-1 (physical frame)                      |
| @ref promeki::Timecode::superFrameIndex                 | 0..fps-1              | 0..(fps/N)-1                                   |
| @ref promeki::Timecode::subFrameIndex                   | 0                     | 0..N-1                                         |
| @ref promeki::Timecode::isHfr                           | `false`               | `true`                                         |
| @ref promeki::Timecode::isSuperFrameBoundary            | always `true`         | true every N physical frames                   |
| @ref promeki::Timecode::Mode::framesPerSuperFrame       | 1                     | 2 / 3 / 4 / 5 depending on rate                |
| @ref promeki::Timecode::Mode::superFrameRate            | tc_fps (24 / 25 / 30) | tc_fps (24 / 25 / 30)                          |

## TimecodeType enum table {#timecode_type_table}

@ref promeki::Timecode::TimecodeType names the digit-family rate
the Timecode counts at.  Each entry maps to one libvtc format
pointer.  Whether the wall-clock rate is integer or NTSC
fractional (1000/1001) is *not* encoded in the TimecodeType — that
distinction lives on the @ref promeki::FrameRate handed to
@ref promeki::Timecode::toRuntime or to @ref promeki::LtcEncoder.

| TimecodeType    | libvtc format          | tc_fps | N | DF? | Wall-clock rate (typical) |
|-----------------|------------------------|--------|---|-----|---------------------------|
| `NDF24`         | `VTC_FORMAT_24`        | 24     | 1 | no  | 24 or 23.976 (NTSC)       |
| `NDF25`         | `VTC_FORMAT_25`        | 25     | 1 | no  | 25                        |
| `NDF30`         | `VTC_FORMAT_30_NDF`    | 30     | 1 | no  | 30 or 29.97 (NTSC)        |
| `DF30`          | `VTC_FORMAT_29_97_DF`  | 30     | 1 | yes | 29.97                     |
| `NDF48`         | `VTC_FORMAT_48`        | 24     | 2 | no  | 48 or 47.952 (NTSC)       |
| `NDF50`         | `VTC_FORMAT_50`        | 25     | 2 | no  | 50                        |
| `NDF60`         | `VTC_FORMAT_60`        | 30     | 2 | no  | 60 or 59.94 (NTSC)        |
| `DF60`          | `VTC_FORMAT_59_94_DF`  | 30     | 2 | yes | 59.94                     |
| `NDF72`         | `VTC_FORMAT_72`        | 24     | 3 | no  | 72                        |
| `NDF96`         | `VTC_FORMAT_96`        | 24     | 4 | no  | 96                        |
| `NDF100`        | `VTC_FORMAT_100`       | 25     | 4 | no  | 100                       |
| `NDF120`        | `VTC_FORMAT_120_30X4`  | 30     | 4 | no  | 120 or 119.88 (NTSC)      |
| `DF120`         | `VTC_FORMAT_119_88_DF` | 30     | 4 | yes | 119.88                    |
| `NDF120_24x5`   | `VTC_FORMAT_120_24X5`  | 24     | 5 | no  | 120                       |

Three drop-frame rates exist in the standards (29.97 DF, 59.94 DF,
119.88 DF — every NTSC fractional rate where the digit math
overruns wall-clock time without compensation).  The NDF variants
at HFR (NDF50, NDF60, NDF120, etc.) never need DF compensation
because their integer wall-clock rate (50, 60, 120) matches the
digit math exactly.

### Digit-family vs wall-clock rate {#timecode_digit_family}

The TimecodeType captures the **digit family** — what numbers
print on screen.  An NDF30 Timecode at frame 30 of second 0 reads
"00:00:00:30" regardless of whether the underlying camera ran at
exactly 30 fps or 29.97 fps.  That ambiguity is unimportant for
arithmetic (digit math is identical at both rates) but matters for
wall-clock conversion.

@ref promeki::Timecode::toRuntime takes an explicit @ref
promeki::FrameRate parameter to convert digits to a wall-clock
@ref promeki::Duration:

```cpp
Timecode tc(Timecode::NDF30, 0, 1, 2, 0);   // "00:01:02:00"
auto d29 = tc.toRuntime(FrameRate(30000, 1001));   // 62.062... s
auto d30 = tc.toRuntime(FrameRate(30, 1));         // exactly 62 s
```

@ref promeki::LtcEncoder takes a similar FrameRate parameter at
construction so it can pick between integer and NTSC libvtc
formats when both apply to the same digit family.

## TimecodeUserbits {#timecode_userbits}

@ref promeki::TimecodeUserbits is the value type for the 32 user
bits SMPTE 12M carries alongside the time-address word.  The
class composes:

- **32 user bits** (eight 4-bit nibbles, low-bit first per spec).
- **A 3-bit BGF mode triple** (BGF2 BGF1 BGF0 → eight modes per
  ST 12-1 Table 1) that tells the receiver how to interpret the
  nibbles.

The mode is a load-bearing field because the same 32 bits mean
different things under different modes.  TimecodeUserbits keeps
factory-style constructors instead of an implicit one so the
caller is forced to name the mode at construction time:

| Factory                                                          | Mode produced                          | Use case                                                  |
|------------------------------------------------------------------|----------------------------------------|-----------------------------------------------------------|
| `TimecodeUserbits::fromRawBits(uint32_t, Mode)`                  | the supplied mode                      | Wire-bytes-in, mode-driven-by-context.                    |
| `TimecodeUserbits::fromNibbles(const Nibbles &, Mode)`           | the supplied mode                      | Hand-built nibble values; codec parse paths.              |
| `TimecodeUserbits::fromAsciiChars(const String &)`               | `EightBitChar` (BGF=001)               | 4-char tags / clip names per ST 12-1 §8.2.                |
| `TimecodeUserbits::fromDateTimeZone(...)`                        | `DateTimeZone` (BGF=100) — stub        | ST 309 date/time packing — placeholder, returns NotSupported. |
| `TimecodeUserbits::reinterpret(Mode)`                            | the supplied mode                      | Keep the 32 bits, change only the interpretation.         |

### BGF mode interpretation (ST 12-1 Table 1) {#timecode_bgf_table}

The `(BGF2, BGF1, BGF0)` triple selects one of eight interpretations
of the 32 user bits.  Modes 100 / 110 (= 4 / 6 in the libpromeki
encoding `(BGF2 BGF1 BGF0)`) reference a clock-time source per
ST 12-1 §8.2.4 and underpin the @ref promeki::TimecodeUserbits::hasClockTimeReference
helper.

| Mode value  | BGF triple (BGF2 BGF1 BGF0) | Name                  | Bit interpretation                                                                   |
|-------------|-----------------------------|-----------------------|--------------------------------------------------------------------------------------|
| 0 (`0b000`) | 0 0 0                       | `Unspecified`         | Application-defined; receiver MUST NOT assume any standard layout.                   |
| 1 (`0b001`) | 0 0 1                       | `EightBitChar`        | Four 8-bit ISO 646 / IRV (typically ASCII) characters across the eight nibbles.      |
| 2 (`0b010`) | 0 1 0                       | `Reserved2`           | Reserved.                                                                            |
| 3 (`0b011`) | 0 1 1                       | `PageLine`            | ST 12-1 §8.2.3 page/line identifier.                                                 |
| 4 (`0b100`) | 1 0 0                       | `DateTimeZone`        | ST 309 date + time-zone — references a clock-time source.                            |
| 5 (`0b101`) | 1 0 1                       | `Reserved5`           | Reserved.                                                                            |
| 6 (`0b110`) | 1 1 0                       | `DateTimeZoneClock`   | ST 309 date + time-zone with a separate time-of-day clock — clock-time reference.    |
| 7 (`0b111`) | 1 1 1                       | `Reserved7`           | Reserved.                                                                            |

The flag bit positions match libvtc's
`VTC_TC_FLAG_LTC_BGF0` / `BGF1` / `BGF2` so the toVtc /
fromVtc helpers can move them between value types without
re-indexing.

### TimecodeUserbits on Timecode {#timecode_userbits_on_tc}

@ref promeki::TimecodeUserbits is embedded by value on
@ref promeki::Timecode (since Phase 2 of the
[timecode audit](../devplan/proav/timecode.md)).  Most call sites
need both digits and user bits at the same time; carrying them
together removes a class of "captured ATC has user bits but the
synthesised Timecode round-trip drops them" bugs.

The color-frame flag (ST 12-1 §8.3.2 bit 11 of the time-address
word) is similarly embedded on Timecode — it's part of the
time-address word, not the ATC envelope.

## ATC carriage {#timecode_atc_carriage}

The wire-format details for ATC live in the
[ANC documentation](anc.md#anc_atc_carriage).  The short version:

- ≤30 fps and the four pair-rates (48 / 50 / 60 / 59.94): ST 12-2
  ATC_LTC / ATC_VITC1 / ATC_VITC2 carriage.
- ≥72 fps (72 / 96 / 100 / 120 / 119.88): ST 12-3 ATC_HFRTC
  carriage (SDID=0x61).

The codec chooses the right carriage automatically from the
Timecode's rate; callers pick the format ID and the codec does
the rest.

## See also

- @ref promeki::Timecode — the digit-only value type.
- @ref promeki::TimecodeUserbits — the user-bits + BGF mode value type.
- @ref promeki::TimecodeGenerator — frame-rate driven synthesis with
  forward / reverse / still / jam transports.
- @ref promeki::LtcEncoder, @ref promeki::LtcDecoder — audio LTC
  pack / unpack with chunked per-video-frame slicing.
- @ref promeki::AncAtc — the ATC envelope value type.
- @ref anc — the ancillary-data framework that hosts the ATC codecs.

@see promeki::Timecode, promeki::TimecodeUserbits,
     promeki::TimecodeGenerator, promeki::LtcEncoder,
     promeki::LtcDecoder, promeki::AncAtc, promeki::FrameRate,
     @ref anc
