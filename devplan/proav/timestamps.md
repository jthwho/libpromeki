# MediaTimeStamp / ClockDomain / Timestamp infrastructure

**Library:** `promeki`
**Status:** Foundation in place; follow-up items remain.

## Shipped

- `ClockDomain` (TypeRegistry-backed wrapper, opaque `Data`,
  `ClockEpoch::PerStream` / `Correlated` / `Absolute`,
  cross-stream / cross-machine comparability helpers, metadata).
- `MediaTimeStamp` (TimeStamp + ClockDomain + offset; Variant +
  DataStream; `toString` / `fromString`).
- `MediaIO` auto-stamping: `Synthetic` fallback on read and write
  paths for any payload missing a timestamp; one-frame duration
  fallback on video payloads with zero duration (Phase 4w).
- V4L2 backend stamps with V4L2 kernel clock (audio with ALSA or
  V4L2 depending on drift correction).
- RTP backend stamps every capture path; `ts-refclk` parsed from SDP
  into the right `ClockDomain` (PTP → Absolute, local → Correlated,
  absent → SystemMonotonic). PTP grandmaster EUI-64 stored on
  Stream and stamped on payload metadata. SDP generation writes
  `ts-refclk` and `mediaclk` for Absolute domains.
- NDI backend (Phase 4z-ndi) — sender-anchored audio timeline
  tracker (`AudioMarker` / `AudioMarkerList` / `pushSilence` /
  `PacingGate`); see [proav/backends.md](backends.md) → NdiMediaIO.
- `EUI64` class (8-byte IEEE identifier; modified EUI-64 MAC
  conversion; three string formats; `std::formatter` specifiers
  `{:h}`, `{:o}`, `{:v}`; Variant + DataStream).
- Metadata IDs: `CaptureTime`, `PresentationTime` (now
  `TypeMediaTimeStamp`), plus `MediaTimeStamp`, `RtpTimestamp`,
  `RtpPacketCount`, `PtpGrandmasterId`, `PtpDomainNumber`.

## Remaining

- [ ] **RTP TX timestamp source config.** The RTP sender should not
  use `Synthetic` auto-stamps. TX timestamps should come from the
  sender's clock (PTP-derived for ST 2110). Add a `MediaConfig` key
  that declares where TX timestamps come from so the sender stamps
  them appropriately.
- [ ] **AudioBuffer resampling sample accounting.** `AudioBuffer`
  should return the number of samples added/removed during
  drift-correction resampling so callers can reconstruct accurate
  timestamps for audio that passed through the resampler (timing
  information currently lost in the FIFO).
- [ ] **PTP domain number in SDP.** `ts-refclk` parsing extracts
  profile + grandmaster but does not parse / propagate the PTP
  domain number. Add when multi-domain PTP support is needed.
- [ ] **TPG / ImageFile backends** fall through to Synthetic
  auto-stamp (correct for now). If they ever gain a real clock
  source, they should stamp directly.
- [ ] **`ClockDomain` metadata include cycle.** `ClockDomain::Data`
  is opaque to avoid a Metadata → Variant → MediaTimeStamp →
  ClockDomain include cycle. When the include structure is
  refactored (e.g. moving Variant to a separate header from its
  type includes), the `Data` struct can be made visible.
- [ ] **`MediaDesc` carries the timestamp relationship.** Today
  every payload's `MediaTimeStamp` is independent. `MediaDesc`
  should declare which `ClockDomain` the image and audio streams
  come from and any fixed offset between them, so consumers don't
  have to infer timing geometry from the first arriving payloads.
  Touches `MediaDesc`, `ImageDesc`, `AudioDesc`, and the per-stage
  `expectedDesc()` plumbing.
