# MediaTimeStamp / ClockDomain / Timestamp Infrastructure

## Status: In Progress

Core infrastructure is implemented and wired into V4L2, RTP, and MediaIO auto-stamping. Several follow-up items remain.

## Completed

- **ClockDomain** — TypeRegistry-pattern class with StringRegistry IDs, opaque Data in registry, epoch tracking (PerStream / Correlated / Absolute), cross-stream/cross-machine comparability helpers, metadata container.
- **ClockEpoch** — TypedEnum with PerStream, Correlated, Absolute values.
- **MediaTimeStamp** — TimeStamp + ClockDomain + Duration offset. Variant type, DataStream serializable, toString/fromString round-trip.
- **Metadata IDs** — `CaptureTime` and `PresentationTime` upgraded to TypeMediaTimeStamp. New: `MediaTimeStamp`, `RtpTimestamp`, `RtpPacketCount`, `PtpGrandmasterId`, `PtpDomainNumber`.
- **MediaIO auto-stamping** — Synthetic fallback on read and write paths for any essence missing a MediaTimeStamp.
- **V4L2 backend** — Video stamped with V4L2 kernel clock, audio with ALSA or V4L2 clock (depends on drift correction).
- **RTP backend** — All capture paths stamped (video, audio aggregated/standalone, data). ts-refclk parsed from SDP into ClockDomain (PTP = Absolute, local = Correlated, absent = SystemMonotonic). PTP grandmaster EUI-64 parsed and stored on Stream, stamped on Image metadata. SDP generation writes ts-refclk and mediaclk for Absolute domains.
- **EUI64 class** — 8-byte IEEE identifier. Modified EUI-64 MAC conversion (FF:FE insertion + U/L bit flip). Three string formats via EUI64Format enum (OctetHyphen, OctetColon, IPv6). Custom std::formatter with {:h}, {:o}, {:v} specifiers. Variant + DataStream integration.
- **MacAddress + EUI64 Variant** — Both added to PROMEKI_VARIANT_TYPES_NETWORK.
- **Audio::metadata()** — Mutable accessor added (matching Image pattern).

## Remaining Work

### TypedEnum constexpr refactor
Make TypedEnum entries `static constexpr` by adding a constexpr int-only constructor to Enum and lazy type registration in the TypedEnum template. Eliminates out-of-class `inline const` definitions. Touches Enum, TypedEnum, and all TypedEnum subclasses in enums.h. See conversation notes for the proposed design (constexpr constructor + `registerSelf()` + lazy `type()` accessor).

### RTP TX timestamp source config
The RTP sender should not use Synthetic auto-stamps. TX timestamps should come from the sender's clock (e.g. PTP-derived for ST 2110). Need a MediaConfig key that declares where TX timestamps come from so the sender stamps them appropriately.

### AudioBuffer resampling sample accounting
AudioBuffer should return the number of samples added/removed as part of drift-correction resampling. This delta could reconstruct accurate timestamps for audio that passed through the resampler, recovering timing information currently lost in the FIFO.

### ClockDomain metadata include cycle
ClockDomain::Data is opaque to avoid a Metadata → Variant → MediaTimeStamp → ClockDomain include cycle. When the include structure is refactored (e.g. moving Variant to a separate header from its type includes), the Data struct could be made visible in the header.

### Other MediaIO backends
- TPG and ImageFile fall through to Synthetic auto-stamp (correct for now).
- If these backends gain real clock sources in the future, they should stamp directly.

### PTP domain number in SDP
The current ts-refclk parsing extracts the PTP profile (IEEE1588-2008/2019) and grandmaster but does not parse or propagate the PTP domain number. When multi-domain PTP support is needed, add domain number extraction from SDP or configuration.

### FramePacer / ClockDomain unification
ClockDomain is intended to absorb the FramePacerClock interface (nowNs, resolutionNs, sleepUntilNs) so a single object describes both identity and active capabilities. This requires adding virtual or function-pointer-based clock operations to ClockDomain's Data.
