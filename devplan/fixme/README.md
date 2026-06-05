# Existing FIXMEs

**Standards:** All fixes must follow `CODING_STANDARDS.md`. All
changes require updated unit tests. See [`devplan/README.md`](../README.md)
for the full requirements.

Tracked FIXME comments scattered across the codebase. Each entry
lives in its own file so individual items can be claimed, edited,
and merged independently. Address these as they become relevant to
ongoing phase work (e.g., fix the File Windows code when
refactoring File to derive from IODevice).

## Core

- [windows-file.md](windows-file.md) — Windows `File` backend is a
  stub.
- [datetime-number-words.md](datetime-number-words.md) — `DateTime`
  parser should use `String::parseNumberWords()`.
- [std-wrappers.md](std-wrappers.md) — replace direct
  `std::vector` / `std::map` / `std::array` usage with library
  wrappers.
- [audiodesc-ctor-arg-order.md](audiodesc-ctor-arg-order.md) —
  `AudioDesc(rate, channels)` is easy to misuse via implicit
  numeric conversions.
- [variantlookup-compiled-path.md](variantlookup-compiled-path.md)
  — `VariantLookup::resolve()` re-parses dotted keys on every
  call; needs a compiled-path type.

## ProAV — audio

- [audiogen-planar.md](audiogen-planar.md) — `AudioGen` only writes
  interleaved planes correctly.

## ProAV — QuickTime / MP4

- [quicktime-raw-byteorder.md](quicktime-raw-byteorder.md) — `raw `
  24-bit RGB / BGR byte-order disagreement across players.
- [quicktime-trex-defaults.md](quicktime-trex-defaults.md) —
  fragmented MP4 reader ignores `trex` defaults.
- [quicktime-xmp-bext.md](quicktime-xmp-bext.md) — XMP parser only
  matches the `bext:` prefix (blocked on core XML support).

## ProAV — JPEG XS

- [jpegxs-svt-packed-rgb.md](jpegxs-svt-packed-rgb.md) — SVT
  validation bug forces packed-RGB → planar via CSC workaround.
- [jpegxs-color-variants.md](jpegxs-color-variants.md) — only
  Rec.709 limited-range variants registered.
- [jpegxs-quicktime-container.md](jpegxs-quicktime-container.md) —
  `jxsm` sample entry not implemented (blocked on procuring
  ISO/IEC 21122-3:2024).
- [jpegxs-rtp-slice-mode.md](jpegxs-rtp-slice-mode.md) — RFC 9134
  slice-mode (K=1) packetization not implemented.

## ProAV — RTP

- [rtp-jpeg-color-info.md](rtp-jpeg-color-info.md) — MJPEG reader
  has no in-band signal for Rec.709 vs Rec.601 / limited vs full
  range.

## ProAV — misc

- [bufferpool-wiring.md](bufferpool-wiring.md) — `BufferPool` exists
  but isn't wired into any hot path.
- [contentlightlevel-fromstring.md](contentlightlevel-fromstring.md)
  — `ContentLightLevel` / `MasteringDisplay` lack `fromString()`
  round-trip parsers.
- [pipelinestats-unit-hint.md](pipelinestats-unit-hint.md) —
  `MediaPipelineStats` picks units by stat-name suffix matching;
  needs an explicit unit hint at registration time.
- [mediapipeline-stats-serial.md](mediapipeline-stats-serial.md)
  — `buildStageStats()` waits on each stage's `stats()` request
  serially; should issue then await.

## SDL

- [sdl-event-pump-idle.md](sdl-event-pump-idle.md) —
  `SdlSubsystem` polls `SDL_PumpEvents` at 60 Hz forever,
  preventing the app from going idle.

## HTTP

- [httpapi-auth.md](httpapi-auth.md) — `HttpApi::Endpoint` has no
  authentication / authorization story; security fields,
  enforcement, and OpenAPI exposure are missing.

## Build

- [cmake-sdl-abi-tracking.md](cmake-sdl-abi-tracking.md) —
  incremental builds miss header-layout changes across shared
  libraries (manifests as ABI-mismatch segfaults).
