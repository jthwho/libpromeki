# JPEG XS: additional matrix / range / colour-space variants

**Files:** `src/core/pixelformat.cpp`,
`src/proav/jpegxsimagecodec.cpp`

**FIXME:** The first-pass codec exposes only the Rec.709
limited-range YUV family
(`JPEG_XS_{YUV8,YUV10,YUV12}_{422,420}_Rec709`). JPEG XS itself is
colour-space agnostic — matrix / range / primaries live out-of-band
in the container (ISO/IEC 21122-3 sample entry) or the SDP
(RFC 9134 `a=fmtp`), never in the bitstream — so adding more
variants is purely a bookkeeping exercise once real workflows need
them.

Likely additions when an upstream caller actually asks for them:

- `JPEG_XS_*_Rec601` / `JPEG_XS_*_Rec601_Full` — legacy broadcast /
  strict JFIF analogues.
- `JPEG_XS_*_Rec709_Full` — modern full-range YUV from cameras with
  an ICC profile.
- `JPEG_XS_*_Rec2020` — HDR / UHD contribution, including the 10-
  and 12-bit planar variants.
- `JPEG_XS_*_SemiPlanar_*` (NV12 / NV16) — if a zero-copy path from
  a hardware decoder wants to avoid the deinterleave cost on the
  input side (SVT needs planar, so the codec would have to run the
  NV-planar → fully-planar split like the JPEG codec already does).

## Tasks

- [ ] Wait for a concrete upstream need before expanding the
  matrix; the current Rec.709 limited-range default matches
  ST 2110 JPEG XS carriage.
- [ ] When adding, follow the JPEG variant pattern in
  `pixelformat.cpp` (`makeJPEG_XS_YUV` helper already structured for
  this) and extend `classifyInput()` / `defaultDecodeTarget()`
  accordingly.
- [ ] Extend the codec's validation in `decode()` to match on the
  new uncompressed targets.
