# RTP JPEG reader: no in-band signal for Rec.709 or limited/full range

**Files:** `src/proav/mediaiotask_rtp.cpp` (`emitVideoFrame` deferred
JPEG geometry)

**FIXME:** When the RTP JPEG reader discovers image geometry from
the first reassembled frame, it correctly detects subsampling
(4:2:2 vs 4:2:0 from the RFC 2435 Type field / SOF0 sampling
factors) and RGB (SOF0 component structure + Type ≥ 2), but always
defaults to Rec.601 full range per the JFIF specification. RFC 2435
carries no metadata for the colour matrix (Rec.601 vs Rec.709) or
quantisation range (full vs limited).

JPEG itself supports optional markers that can carry colour
information:

- **APP2 (ICC Profile)** — the most authoritative source; a full
  ICC colour profile can be embedded across chained APP2 segments.
  Definitively identifies primaries, matrix, and transfer
  function.
- **APP14 (Adobe)** — carries a colour transform byte (0=unknown/RGB,
  1=YCbCr, 2=YCCK).
- **APP1 (EXIF)** — `ColorSpace` tag (1=sRGB, 0xFFFF=uncalibrated).
- **APP0 (JFIF)** — its presence implies Rec.601 full-range YCbCr
  per the JFIF spec.

However, standard RFC 2435 senders strip all markers and transmit
only the entropy-coded data + quantisation tables. Our own
`RtpPayloadJpeg::unpack()` reconstructs a bare JFIF
(SOI / DQT / SOF0 / DHT / SOS / ECS / EOI) with none of these APP
markers. Non-standard MJPEG-over-RTP implementations that send
complete JFIF frames (typically with a dynamic PT) could include
these markers, but the current reader does not inspect them.

## Tasks

- [ ] Parse APP2 ICC profile from reassembled JFIF when present —
  extract primaries / matrix / TRC and map to the closest
  PixelFormat variant (Rec.601 vs Rec.709, full vs limited).
- [ ] Parse APP14 Adobe marker for the colour transform byte as a
  secondary signal.
- [ ] Consider a `MediaConfig::VideoColorModel` override key so
  callers can force Rec.709 / limited range for broadcast sources
  that use JPEG transport without colour metadata.
