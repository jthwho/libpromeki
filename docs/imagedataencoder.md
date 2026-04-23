# Image Data Encoder Wire Format {#imagedataencoder}

VITC-style binary data encoder for libpromeki Images — wire
format specification, byte order, CRC parameters, and a
worked example for third-party decoders.

The `ImageDataEncoder` class stamps 64-bit opaque payloads into
specific scan-line bands of a libpromeki `Image` as a sequence of
wide black/white "bit cells", in a layout that resembles SMPTE
Vertical Interval Time Code (VITC) but is *not* SMPTE-compliant:
it carries different fields, uses a different sync pattern, and a
different CRC. This page is the normative wire format reference —
anything not described here is implementation-defined and subject
to change.

## Overview {#img_data_overview}

The encoder writes one or more "bands" of scan lines into an image,
where each band carries the same opaque 64-bit payload across every
scan line in the band. Within each scan line, the payload is laid
out as a sequence of equally-wide pixel "cells", each of which
represents one bit of a fixed wire frame:

```
  |  4 sync bits  |       64 payload bits        |  8 CRC bits  | pad |
  | W B W B       | MSB  ............... LSB     | MSB ... LSB  |     |
```

- **Sync nibble** — four cells holding white, black, white, black,
  in that order. Binary `1010`, `0xA`, MSB-first. Provides a
  fixed alignment pattern at the start of every encoded scan line.
- **Payload** — 64 bits of opaque data, transmitted MSB-first. The
  library uses this slot to carry frame numbers, BCD timecode words
  (see `Timecode::toBcd64`), stream identifiers, etc. Anything that
  fits in 64 bits is fair game.
- **CRC** — 8 bits of CRC-8/AUTOSAR computed over the 8 payload
  bytes interpreted as a big-endian word. Transmitted MSB-first.
  Detects single-bit errors and a high fraction of multi-bit
  errors over a 4-byte payload.
- **Padding** — any pixels left over after the `76` cells fit in
  the image width are filled with the format's "black" on luma /
  RGB planes and with the format's neutral midpoint on sub-sampled
  chroma planes.

Total bits per row: `4 + 64 + 8 = 76`.

Each scan line in a band carries an *exact* copy of the same wire
frame; the same band is repeated for `lineCount` scan lines so a
decoder can recover the payload from any single line, or average /
majority-vote across multiple lines for noisy channels.

## Bit and byte ordering {#img_data_bitorder}

The wire is **MSB-first throughout**:

- The sync nibble is transmitted bit 3 first (white), bit 2 (black),
  bit 1 (white), bit 0 (black).
- The payload is transmitted bit 63 first, bit 0 last.
- The CRC is transmitted bit 7 first, bit 0 last.

The CRC is computed over the **payload only** (the sync nibble is
fixed and adds no information; including it in the CRC would just
shift the CRC by a constant). The 64-bit payload is interpreted
as 8 bytes in **big-endian** order before being fed to the CRC
algorithm:

```cpp
uint8_t bytes[8];
for(int i = 0; i < 8; i++) {
    bytes[i] = (payload >> ((7 - i) * 8)) & 0xff;
}
// bytes[0] = MSB of payload, bytes[7] = LSB of payload
```

The CRC parameters match the standard CRC-8/AUTOSAR variant from the
Rocksoft / reveng catalogue:

| Parameter | Value | Notes                                       |
|-----------|-------|---------------------------------------------|
| Width     | 8     |                                             |
| Polynomial| `0x2F` | `x^8 + x^5 + x^3 + x^2 + x + 1`            |
| Init      | `0xFF` | Non-zero so all-zero payloads do not yield CRC=0 |
| RefIn     | false | No input reflection                          |
| RefOut    | false | No output reflection                         |
| XorOut    | `0xFF` | Final XOR                                    |
| Check     | `0xDF` | CRC over ASCII `"123456789"`                |

Reference implementation: `CrcParams::Crc8Autosar` in libpromeki, or
any standards-compliant CRC-8/AUTOSAR implementation. CRC-8/AUTOSAR
has a Hamming distance of 6 at message lengths up to 8 data bytes —
the strongest among the 8-bit CRCs commonly available.

## Bit cell width selection {#img_data_cellwidth}

The bit cell width (in pixels) is chosen at encoder construction
time as the largest value that satisfies these two constraints:

1. `76` cells fit on the scan line:
   `bitWidth * 76 <= imageWidth`.
2. `bitWidth` is a multiple of the format's natural alignment
   quantum, computed as
   `lcm(pixelsPerBlock, hSubsampling for every plane)`.

The alignment quantum guarantees that adjacent cells start on
properly-aligned byte offsets in every plane, so the encoder's hot
path can use a single `memcpy` per cell with no per-cell shifting
or masking. Examples:

| Format                           | pixelsPerBlock | chroma hSub | quantum | 1920 px → bitWidth |
|----------------------------------|---------------:|------------:|--------:|-------------------:|
| RGBA8 / BGRA8 / RGB8 / Mono8     | 1              | 1           | 1       | 25                |
| YUV8_422 (YUYV) interleaved      | 2              | 1\*         | 2       | 24                |
| YUV8_422 / YUV8_420 planar       | 1 (default)    | 2           | 2       | 24                |
| YUV8_420 NV12 (semi-planar)      | 1              | 2           | 2       | 24                |
| YUV10_422 v210                   | 6              | 1\*         | 6       | 24                |

(\*) — interleaved YCbCr macropixels carry chroma in-line at full
width within the single plane, so the plane's `hSubsampling` is
left at 1 even though the *format* is 4:2:2.

The trailing pad width is then `imageWidth - 76 * bitWidth` pixels,
rounded down to the same alignment quantum (any sub-quantum
leftovers at the end of the line are left untouched by the encoder
and retain whatever the rest of the application has put there).

If the image is too narrow to fit a single `76`-cell row at the
format's quantum, the encoder fails construction
(`ImageDataEncoder::isValid` returns `false`).

## Per-format value mapping {#img_data_value_mapping}

The encoder uses libpromeki's [CSC pipeline](csc.md) to render its
three "primer" cells once at construction time, then memcpys them
into place per scan line. The primers are derived from these
source colors:

| Primer  | Source RGB         | Used for                                           |
|---------|--------------------|----------------------------------------------------|
| white   | `(255, 255, 255)`  | "1" cells on luma / RGB planes                     |
| black   | `(0, 0, 0)`        | "0" cells on luma / RGB planes; trailing pad       |
| neutral | `(128, 128, 128)`  | Both "1" and "0" cells *and* pad on chroma planes  |

The neutral primer is used on **any plane that is sub-sampled**
(`hSubsampling > 1` or `vSubsampling > 1`). The reason is
subtle: the CSC pipeline's float-to-integer rounding can land
white-cell chroma and black-cell chroma on neighbouring integers
(e.g. white→Cb=127, black→Cb=128 for limited-range Rec.709), which
would otherwise create visible chroma flicker between cells. Using
a single neutral primer for the chroma plane forces a uniform value
across the entire encoded row, so the bit pattern only ever
modulates luma.

For a typical 8-bit limited-range Rec.709 destination, the encoded
row therefore looks like:

| Plane         | "1" cell | "0" cell | Pad    |
|---------------|---------:|---------:|-------:|
| Y             | `235`    | `16`     | `16`   |
| Cb            | `128*`   | `128*`   | `128*` |
| Cr            | `128*`   | `128*`   | `128*` |

(\*) — exact value is whatever the CSC pipeline produces from
mid-gray RGB, typically within ±1 of the textbook midpoint. Decoders
should not assume any specific chroma value; they should ignore the
chroma planes entirely and read bits from the luma plane only.

For 10-bit and 12-bit YCbCr formats the same scheme applies, scaled
to the bit depth: limited-range 10-bit Y' is roughly `64` (black) /
`940` (white).

For RGB formats every plane is full-range, so the white and black
primers are the format's component-wise max and min respectively
(e.g. `0/255` for 8-bit, `0/65535` for 16-bit).

## How to write a decoder {#img_data_decode}

A minimal decoder for a known `PixelFormat` and image size proceeds as
follows:

1. Compute the bit cell width using the same algorithm the encoder
   uses ([Bit cell width selection](#img_data_cellwidth) above).
2. Pick a single luma scan line that should be inside the encoded
   band. Read `bitWidth` bytes (or one byte per cell — only the
   luma value at the start of each cell matters) at the relevant
   positions:

   - For interleaved YUYV: byte `(cellIdx * bitWidth * 2)` is the Y
     sample at the start of the cell.
   - For interleaved RGB: byte `(cellIdx * bitWidth * bytesPerPixel)`
     is the R / G / B sample at the start of the cell.
   - For v210: the first 32-bit word of the cell, in little-endian
     byte order, packs `(Cb0 | Y0 | Cr0 | xx)` into bits
     `(0..9 | 10..19 | 20..29 | 30..31)`. Y0 is bits 10..19.
   - For planar / semi-planar: the luma plane's pointer plus
     `(line * lineStride)` gives a pointer to the line; cell `c`
     starts `(c * bitWidth * lumaBytesPerSample)` bytes into it.

3. Establish a "white reference" by reading the byte at cell index 0
   (which is the first sync bit and therefore guaranteed to be
   white). Treat any cell whose sample is "close enough to" the
   white reference as a 1, and any other cell as a 0. In practice
   a simple equality check works because the encoder writes the
   cells via `memcpy` from a pre-rendered primer — every white
   cell holds an exact copy of the same primer bytes.

4. Read 76 cells into a 76-bit shift register, MSB-first. Verify
   the top 4 bits equal the sync nibble `0xA`. Extract bits
   `71..8` as the 64-bit payload and bits `7..0` as the 8-bit
   CRC.

5. Recompute the CRC over the 8 payload bytes (big-endian) using
   CRC-8/AUTOSAR and compare to the wire CRC. A mismatch indicates
   the row was corrupted in transit and should be discarded;
   decoders that have multiple band scan lines available can try
   another scan line from the same band.

## Alignment notes {#img_data_alignment}

The encoder writes whole bands of scan lines, where each band is
defined by a `(firstLine, lineCount, payload)` triple. When the
destination format has vertical chroma sub-sampling (4:2:0
variants), the chroma plane has fewer scan lines than the luma
plane, and any chroma row that overlaps the luma band is written
with the neutral primer. Bands whose `firstLine` is not a
multiple of the chroma vertical sub-sampling (e.g. `firstLine = 1`
for a 4:2:0 image) will cause the chroma row containing luma line 0
to be touched as well — typically harmless because the chroma is
neutral and idempotent across cells, but worth keeping in mind if
the bands meet at a non-chroma-aligned boundary.

The recommended layout is to align all band boundaries to multiples
of the maximum `vSubsampling` in the format (typically 2 for 4:2:0
variants, 1 otherwise). libpromeki's TPG always emits bands at
multiples of 16 scan lines, which satisfies any sub-sampling
pattern up to 16:1 vertical.

## Worked example {#img_data_example}

Consider a 1920×1080 RGBA8_sRGB image with two bands:

- Lines `0..15` carry payload `0x0123456789ABCDEF`.
- Lines `16..31` carry the BCD timecode word for `01:00:00:00` in
  29.97 NDF, which is `0x0001000000000000` (hour units = 1 at
  bits 48..51 of the 64-bit word, every other field zero) per
  `Timecode::toBcd64` with `TimecodePackFormat::Vitc` mode.

The bit cell width is `1920 / 76 = 25` (alignment quantum 1 for
RGBA8), so the first band of scan lines looks like:

```
  bytes 0..99      (cells 0..3)  : sync   1010
  bytes 100..1699  (cells 4..67) : payload 0x0123456789ABCDEF MSB-first
  bytes 1700..1899 (cells 68..75): CRC-8/AUTOSAR over 0x01..EF
  bytes 1900..7679                 : padding (black RGBA)
```

Each cell is `25 * 4 = 100` bytes (25 RGBA pixels), and
`76 * 100 = 7600` bytes leaves `1920 * 4 - 7600 = 80` bytes of pad
at the right edge of every encoded scan line.

The CRC for payload `0x0123456789ABCDEF` can be computed against
any standards-compliant CRC-8/AUTOSAR implementation:

```cpp
#include <promeki/crc.h>
using namespace promeki;

uint8_t bytes[8] = { 0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF };
uint8_t crc = Crc8::compute(CrcParams::Crc8Autosar, bytes, 8);
```

## Why not actual VITC? {#img_data_format_history}

SMPTE VITC (12M-2) is a 90-bit-per-line wire format with its own
sync words, CRC polynomial, and bit positions. This encoder is
deliberately *not* compliant with that standard for several reasons:

- VITC carries SMPTE timecode digits as its only payload, with no
  provision for arbitrary opaque data. We need to stamp things
  like rolling frame numbers, stream IDs, and other application
  identifiers into images, where SMPTE digits would be a poor fit.
- VITC uses a fixed bit-cell width tuned for analog NTSC line widths
  and a specific oscilloscope-friendly transition pattern. We want
  the cells to scale up to whatever pixel pitch the destination
  image has, and we don't care about analog transition behaviour.
- VITC's 8-bit CRC uses polynomial `0x39`, which has weaker
  error-detection properties than CRC-8/AUTOSAR at our payload
  length.

Callers that need an actual SMPTE-compliant VITC pattern in an
image should look elsewhere; this encoder targets the much simpler
"stamp a few 64-bit words into an image so a tool downstream can
find them again" use case.

**See also:** `ImageDataEncoder`, `CRC`, `CrcParams::Crc8Autosar`,
`Timecode::toBcd64`.
