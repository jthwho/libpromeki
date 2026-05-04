# `PixelAspect` object

Add a `PixelAspect` object (its own header, Variant-registered) to
represent pixel aspect ratio independently of `VideoFormat`.

`VideoFormat` deliberately stays a pure raster + rate + scan triple
— PAR varies with how a raster is captured / displayed (NTSC
720×486 at 4:3 vs 16:9, DCI 2048×1080 anamorphic vs flat, etc.) and
tying it into `VideoFormat` would force every format consumer to
think about display aspect when most don't care.

## Shape

- Storage: `Rational` (10:11, 40:33, 12:11, 16:11, 1:1, …).
- Well-known-PAR enum for common SMPTE / DCI values.
- `displayAspect(const Size2Du32 &raster) const` helper that
  multiplies PAR by the raster aspect.
- Lives alongside `VideoFormat` in core.

## Downstream wiring

- `ImageDesc` gains a `pixelAspect()` field so renderers (SDL
  viewer, image-file writers) can honour it.
- `MediaDesc` exposes it per-image-index.

Not a blocker for current `VideoFormat` work but worth landing
before too many callers assume 1:1 pixels.

## Tasks

- [ ] `include/promeki/pixelaspect.h`, `src/core/pixelaspect.cpp`,
  `tests/unit/pixelaspect.cpp`.
- [ ] Variant + DataStream registration.
- [ ] `ImageDesc::pixelAspect()` field.
- [ ] `MediaDesc` per-image accessor.
- [ ] SDL viewer: scale display rect by `displayAspect()` when
  PAR ≠ 1:1.
