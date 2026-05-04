# QuickTime: `raw ` codec byte order — BGR vs RGB disagreement

**Files:** `src/core/pixelformat.cpp` (`makeRGB8`),
`src/proav/quicktime_writer.cpp` (visual sample entry writer)

**FIXME:** Players disagree on the byte order of the QuickTime `raw `
codec tag with depth 24.

The QuickTime File Format Specification historically defines `raw `
as **B, G, R** byte order per pixel. Modern ffmpeg, VLC, and our own
reader treat `raw ` as **R, G, B** byte order (the order ffmpeg's
`rawvideo` encoder emits). mplayer follows the historical QT spec
and reports `VIDEO: [BGR] 320x180 24bpp` for our files — which means
it will swap red and blue channels on display.

Reproduction: ffmpeg decodes our `RGB8_sRGB` → `raw ` output and
produces correct SMPTE color bars. mplayer opens the same file and
reads the byte layout as BGR.

## Options

- **(a)** Switch our `RGB8_sRGB` plane layout to BGR and swap on
  encode. Matches the official QT spec but requires byte-swapping on
  every frame write (and a full code-path audit for anything that
  touches `RGB8_sRGB` bytes directly).
- **(b)** Use a different `PixelFormat` whose QuickTime FourCC
  unambiguously encodes the byte order (e.g. a dedicated `BGR8_sRGB`
  entry with `raw ` as its QT FourCC, and keep `RGB8_sRGB` for paths
  that write to containers that use RGB order natively).
- **(c)** Route 24-bit RGB through a different QuickTime codec tag
  that modern players all agree is RGB (e.g. emit the rarer `BGR `
  four-letter code or use a proprietary FourCC and hope for the
  best — not recommended).

Short-term mitigation: test playback in VLC / ffplay / QuickTime
Player (the widely-used players all agree on RGB for `raw ` 24-bit).
mplayer is the outlier. Document the disagreement rather than chase
the long tail.

## Tasks

- [ ] Pick option (a) or (b) — lean toward (b) for cleanliness, since
  it keeps `RGB8_sRGB` meaning what it says and isolates the
  container-specific byte order to a separate `PixelFormat`.
- [ ] Add a mplayer playback test once one of the above fixes is in
  place.
