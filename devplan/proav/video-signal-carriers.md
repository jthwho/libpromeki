# Video Signal Carrier Types

**Library:** `promeki` (always built — no PROAV gate; generic
content-agnostic carrier descriptors)
**Standards:** All work follows `CODING_STANDARDS.md`. Every new
class requires complete doctest coverage. See `devplan/README.md`
for the full requirements.
**Status:** COMPLETE (2026-05-16). Phases 1–5 shipped. Unblocks `devplan/proav/ntv2.md` Phase 1.

## Goal

Ship the missing **carrier-level** descriptors that name "which
physical connector(s) on which device, carrying what link
standard, locked to which reference clock" — independent of
content (which `VideoFormat`, `ImageDesc`, `AudioDesc` already
cover). These are tiny value types in the spirit of
`Size2Du32` / `FrameRate` / `Url` / `AncDesc`, designed to be
held in `MediaConfig` keys and passed around pipelines unchanged.

They are split out from the AJA NTV2 backend work
([`ntv2.md`](ntv2.md)) into their own milestone because:

- They are not NTV2-specific. The first consumer is the NTV2
  MediaIO backend, but DeckLink, ST 2022-6, ST 2110-20, and any
  future hardware-SDI / SDI-over-IP backend reuse them unchanged.
  Building them under `ntv2*` filenames or gating them behind
  `PROMEKI_ENABLE_NTV2` would force a refactor as soon as the
  second consumer lands.
- They are independently useful for tooling (`mediaplay --probe`
  needs a way to describe an SDI input independent of what
  hardware exposes it; `mediaio` configuration JSON wants
  human-readable port specs).
- They are small enough to ship as their own changeset with full
  test coverage; folding them into the NTV2 work would slow
  review.
- They have no AJA SDK dependency, so they build and test on
  every host configuration — useful for CI coverage that doesn't
  include the AJA card.

## Naming policy

**As shipped:** `SdiLinkStandard` uses short topological prefix forms
(`SL_` single-link, `DL_` dual-link, `QL_` quad-link) rather than the
verbose SMPTE-literal names originally planned (`SmpteSd259`,
`SmpteHd292`, etc.). The short forms proved unambiguous in practice and
are substantially easier to type in JSON / CLI usage. HDMI uses the CTA /
HDMI Forum spec-version vocabulary (`Hdmi14`, `Hdmi20`, `Hdmi21`).
Reference sources use generic vendor-neutral names (`FreeRun`, `Genlock`,
`External`, `FromSignal`). `VideoReferenceRateFamily` values are `Integer`
and `Fractional` (not the verbose `Integer_148_5MHz` forms originally
planned).

## Conventions in use

All four classes follow the **current** library conventions
(post-2026-05-13):

- **CoW value-type handle.** Outer class holds `SharedPtr<Impl> _d`
  (the default `CopyOnWrite=true` template parameter is implicit).
  Inner `Impl` struct carries `PROMEKI_SHARED_FINAL(Impl)`.  Copy
  of the outer = refcount bump; mutators detach via CoW.  No
  `::Ptr` alias on the outer class (per the post-2026-05-07
  convention — see `[[feedback_prefer_own_classes]]` and the
  `AncDesc` reference impl).
- **`PROMEKI_DATATYPE(TYPE, DataTypeID, VERSION)`** in the class
  body so the `DataType` registry picks up the id / name / version
  trait for `Variant`, `DataStream`, `JSON`, and `String`
  round-trip discovery.
- **Built-in registration** in `src/core/datatype.cpp` —
  `registerBuiltinDataTypes()` gets one `registerDataType<T>();`
  line per new type, and `datatype.h` gets the matching
  `DataTypeID` enum value (next free IDs start at `0x64`).
- **Free `operator<<` / `operator>>`** for `DataStream` per the
  current pattern (e.g. `ancdesc.h:332-335`), and the JSON / String
  hooks auto-wire through the macro trait.
- **TypedEnum** for every enum, in `enums.h`, per
  `[[feedback_typedenum_enums_h]]`.

The `AncDesc` class (`include/promeki/ancdesc.h` + `src/proav/ancdesc.cpp`)
is the **reference impl** to model the four new classes on — same
shape, same idioms, same wire-format conventions.

## File layout (new, all in the flat header tree)

```
include/promeki/
├── videoportref.h          # VideoPortRef (CoW value type) +
│                           #   VideoConnectorKind TypedEnum
├── sdisignalconfig.h       # SdiSignalConfig (CoW value type) +
│                           #   SdiLinkStandard TypedEnum +
│                           #   helper free functions
├── hdmisignalconfig.h      # HdmiSignalConfig (CoW value type) +
│                           #   HdmiSpecVersion TypedEnum
└── videoreferenceconfig.h  # VideoReferenceConfig (CoW value type) +
                            #   VideoReferenceSource TypedEnum +
                            #   VideoReferenceRateFamily TypedEnum

src/core/                   # placement is core, NOT proav, because
                            # they have no PROAV dependency and
                            # `MediaConfig` keys for them are read
                            # by tools that don't link against PROAV
├── videoportref.cpp
├── sdisignalconfig.cpp
├── hdmisignalconfig.cpp
└── videoreferenceconfig.cpp
```

(Open question: are these truly core, or proav?  They describe
*video* I/O, but they have no dependency on any video machinery —
`VideoFormat` lives in core too. See Open Questions.)

## DataType ID allocations

To be assigned in `datatype.h` alongside the existing values
(last in-use is `DataTypeHdrDynamic2094_40 = 0x63`):

```
DataTypeVideoPortRef         = 0x64
DataTypeSdiSignalConfig      = 0x65
DataTypeHdmiSignalConfig     = 0x66
DataTypeVideoReferenceConfig = 0x67
```

## Types in detail

### `VideoPortRef` + `VideoConnectorKind`

Identifies one physical connector on a device.  Scoped to whatever
MediaIO holds it — connector identity is relative to the bound
device, not globally unique.

```cpp
// enums.h
class VideoConnectorKind : public TypedEnum<VideoConnectorKind> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("VideoConnectorKind", 0,
                        {"Auto", 0}, {"Sdi", 1}, {"Hdmi", 2},
                        {"DisplayPort", 3}, {"Composite", 4},
                        {"Component", 5}, {"SVideo", 6}, {"Sfp", 7});

                using TypedEnum<VideoConnectorKind>::TypedEnum;

                static const VideoConnectorKind Auto;
                static const VideoConnectorKind Sdi;
                static const VideoConnectorKind Hdmi;
                static const VideoConnectorKind DisplayPort;
                static const VideoConnectorKind Composite;
                static const VideoConnectorKind Component;
                static const VideoConnectorKind SVideo;
                static const VideoConnectorKind Sfp;
};

// videoportref.h
class VideoPortRef {
        public:
                PROMEKI_DATATYPE(VideoPortRef, DataTypeVideoPortRef, 1)

                VideoPortRef() = default;
                VideoPortRef(VideoConnectorKind kind, int index);

                bool isValid() const;
                VideoConnectorKind kind() const;
                int                index() const;          // 1-based

                void setKind(VideoConnectorKind);          // CoW
                void setIndex(int);                        // CoW

                String toString() const;                   // "sdi1", "hdmi2"
                static Result<VideoPortRef> fromString(const String &);

                bool operator==(const VideoPortRef &) const;
                bool operator!=(const VideoPortRef &) const;
                bool operator<(const VideoPortRef &) const;     // for use in Set/Map

                struct Impl {
                                PROMEKI_SHARED_FINAL(Impl)

                                VideoConnectorKind kind  = VideoConnectorKind::Auto;
                                int                index = 0;
                };

        private:
                SharedPtr<Impl> _d;
};

DataStream &operator<<(DataStream &, const VideoPortRef &);
DataStream &operator>>(DataStream &, VideoPortRef &);
```

### `SdiLinkStandard` + `SdiSignalConfig`

```cpp
// enums.h — as shipped (short prefix forms)
class SdiLinkStandard : public TypedEnum<SdiLinkStandard> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("SdiLinkStandard", 0,
                        {"Auto",       0},
                        {"SL_SD",      1},
                        {"SL_HD",      2},
                        {"DL_HD",      3},
                        {"SL_3GA",     4},
                        {"SL_3GB",     5},
                        {"DL_3GB",     6},
                        {"DL_3G",      7},
                        {"QL_3G_SQD",  8},
                        {"QL_3G_2SI",  9},
                        {"SL_6G",     10},
                        {"SL_12G",    11},
                        {"SL_24G",    12});

                using TypedEnum<SdiLinkStandard>::TypedEnum;

                static const SdiLinkStandard Auto;
                static const SdiLinkStandard SL_SD;
                static const SdiLinkStandard SL_HD;
                // ... etc ...

                // Helpers (free functions in sdisignalconfig.h):
                // cablesFor, isDualLink, isQuadLink, nominalDataRateGbps
};

// sdisignalconfig.h
class SdiSignalConfig {
        public:
                PROMEKI_DATATYPE(SdiSignalConfig, DataTypeSdiSignalConfig, 1)

                using PortList = ::promeki::List<VideoPortRef>;

                SdiSignalConfig() = default;
                SdiSignalConfig(SdiLinkStandard standard, PortList ports);

                bool isValid() const;
                SdiLinkStandard standard() const;
                const PortList &ports() const;
                int             cableCount() const;        // ports().size()

                void setStandard(SdiLinkStandard);         // CoW
                void setPorts(PortList);                   // CoW
                void appendPort(VideoPortRef);             // CoW

                // Convenience factories (do not allocate the Impl until
                // the resulting object is mutated — the Impl shared
                // refcount + the CoW handle make these cheap).
                static SdiSignalConfig singleLink(SdiLinkStandard, VideoPortRef);
                static SdiSignalConfig dualLink(SdiLinkStandard,
                                                VideoPortRef a, VideoPortRef b);
                static SdiSignalConfig quadLink(SdiLinkStandard,
                                                VideoPortRef a, VideoPortRef b,
                                                VideoPortRef c, VideoPortRef d);

                // Sanity-checks the cable count matches the standard's
                // cablesFor() value.  Returns Error::Ok for valid,
                // Error::InvalidArgument with a descriptive message
                // otherwise.
                Error validate() const;

                String toString() const;
                // "smpte12g2082:sdi1"
                // "smptequadlink3g2si:sdi1+sdi2+sdi3+sdi4"
                static Result<SdiSignalConfig> fromString(const String &);

                bool operator==(const SdiSignalConfig &) const;
                bool operator!=(const SdiSignalConfig &) const;

                struct Impl {
                                PROMEKI_SHARED_FINAL(Impl)

                                SdiLinkStandard standard = SdiLinkStandard::Auto;
                                PortList        ports;
                };

        private:
                SharedPtr<Impl> _d;
};

DataStream &operator<<(DataStream &, const SdiSignalConfig &);
DataStream &operator>>(DataStream &, SdiSignalConfig &);
```

### `HdmiSpecVersion` + `HdmiSignalConfig`

```cpp
// enums.h
class HdmiSpecVersion : public TypedEnum<HdmiSpecVersion> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("HdmiSpecVersion", 0,
                        {"Auto",   0}, {"Hdmi14", 1},
                        {"Hdmi20", 2}, {"Hdmi21", 3});

                using TypedEnum<HdmiSpecVersion>::TypedEnum;

                static const HdmiSpecVersion Auto;
                static const HdmiSpecVersion Hdmi14;
                static const HdmiSpecVersion Hdmi20;
                static const HdmiSpecVersion Hdmi21;
};

// hdmisignalconfig.h
class HdmiSignalConfig {
        public:
                PROMEKI_DATATYPE(HdmiSignalConfig, DataTypeHdmiSignalConfig, 1)

                HdmiSignalConfig() = default;
                HdmiSignalConfig(VideoPortRef port,
                                 HdmiSpecVersion versionHint = HdmiSpecVersion::Auto);

                bool             isValid() const;
                VideoPortRef     port() const;
                HdmiSpecVersion  versionHint() const;

                void setPort(VideoPortRef);                // CoW
                void setVersionHint(HdmiSpecVersion);      // CoW

                String toString() const;       // "auto:hdmi1", "hdmi21:hdmi2"
                static Result<HdmiSignalConfig> fromString(const String &);

                bool operator==(const HdmiSignalConfig &) const;
                bool operator!=(const HdmiSignalConfig &) const;

                struct Impl {
                                PROMEKI_SHARED_FINAL(Impl)

                                VideoPortRef    port;
                                HdmiSpecVersion versionHint = HdmiSpecVersion::Auto;
                };

        private:
                SharedPtr<Impl> _d;
};

DataStream &operator<<(DataStream &, const HdmiSignalConfig &);
DataStream &operator>>(DataStream &, HdmiSignalConfig &);
```

### `VideoReferenceConfig`

```cpp
// enums.h — as shipped
class VideoReferenceSource : public TypedEnum<VideoReferenceSource> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("VideoReferenceSource", 0,
                        {"FreeRun",    0}, {"Genlock",    1},
                        {"External",   2}, {"FromSignal", 3},
                        {"Ptp",        4}, {"Word",       5});

                using TypedEnum<VideoReferenceSource>::TypedEnum;

                static const VideoReferenceSource FreeRun;
                static const VideoReferenceSource Genlock;
                static const VideoReferenceSource External;
                // FromSignal: lock to the signal arriving on one of
                // the device's own connectors (named by the
                // signalPort() field on VideoReferenceConfig).
                static const VideoReferenceSource FromSignal;
                static const VideoReferenceSource Ptp;     // future
                static const VideoReferenceSource Word;    // future
};

class VideoReferenceRateFamily : public TypedEnum<VideoReferenceRateFamily> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("VideoReferenceRateFamily", 0,
                        {"Auto",       0},
                        {"Integer",    1},
                        {"Fractional", 2});

                using TypedEnum<VideoReferenceRateFamily>::TypedEnum;

                static const VideoReferenceRateFamily Auto;
                // 24 / 25 / 30 / 50 / 60 — integer-Hz family.
                static const VideoReferenceRateFamily Integer;
                // 23.976 / 29.97 / 59.94 — NTSC-derived fractional family.
                static const VideoReferenceRateFamily Fractional;
};

// videoreferenceconfig.h
class VideoReferenceConfig {
        public:
                PROMEKI_DATATYPE(VideoReferenceConfig, DataTypeVideoReferenceConfig, 1)

                VideoReferenceConfig() = default;
                VideoReferenceConfig(VideoReferenceSource source,
                                     VideoReferenceRateFamily family
                                         = VideoReferenceRateFamily::Auto);

                bool                      isValid() const;
                VideoReferenceSource      source() const;
                VideoReferenceRateFamily  family() const;
                VideoPortRef              signalPort() const;
                // ... only meaningful when source() == FromSignal ...

                void setSource(VideoReferenceSource);              // CoW
                void setFamily(VideoReferenceRateFamily);          // CoW
                void setSignalPort(VideoPortRef);                  // CoW

                String toString() const;
                // "freerun:auto"
                // "genlock:integer"
                // "fromsignal:sdi1:fractional"
                static Result<VideoReferenceConfig> fromString(const String &);

                bool operator==(const VideoReferenceConfig &) const;
                bool operator!=(const VideoReferenceConfig &) const;

                struct Impl {
                                PROMEKI_SHARED_FINAL(Impl)

                                VideoReferenceSource     source = VideoReferenceSource::FreeRun;
                                VideoReferenceRateFamily family = VideoReferenceRateFamily::Auto;
                                VideoPortRef             signalPort;
                };

        private:
                SharedPtr<Impl> _d;
};

DataStream &operator<<(DataStream &, const VideoReferenceConfig &);
DataStream &operator>>(DataStream &, VideoReferenceConfig &);
```

## `MediaConfig` keys to add

New "Generic video carrier" block in `include/promeki/mediaconfig.h`,
between the existing media-descriptor keys and the per-backend
blocks:

```
SdiInputSignal       SdiSignalConfig         {}             SDI input port + SMPTE link standard.
SdiOutputSignal      SdiSignalConfig         {}             SDI output port + SMPTE link standard.
HdmiInputSignal      HdmiSignalConfig        {}             HDMI input port + version hint.
HdmiOutputSignal     HdmiSignalConfig        {}             HDMI output port + version hint.
VideoReference       VideoReferenceConfig    FreeRun:Auto   Device-wide reference clock config.
```

Each key uses `setType(DataType*SignalConfig*)` and the spec's
default-value is a default-constructed instance.

## Phase plan

Cut points line up with the type families — each phase ships a
working changeset with full doctest coverage and the matching
DataType registration.

### Phase 1 — `VideoPortRef` + `VideoConnectorKind`

- `VideoConnectorKind` `TypedEnum` in `enums.h`.
- `VideoPortRef` CoW value type, header + impl.
- `PROMEKI_DATATYPE` + `DataTypeVideoPortRef = 0x64`
  registered in `datatype.h` + `datatype.cpp`.
- `DataStream` operators, `toString` / `fromString`,
  `Variant::registerBuiltinConverters` entries.
- Doctest: equality, ordering, string round-trip, DataStream
  round-trip, Variant round-trip, JSON round-trip, invalid /
  default handling.

### Phase 2 — `SdiLinkStandard` + `SdiSignalConfig`

- `SdiLinkStandard` `TypedEnum`.
- Free helpers `cablesFor`, `isQuadLink`, `isDualLink`,
  `nominalDataRateGbps` — pure functions, header-inline, fully
  table-driven and doctested.
- `SdiSignalConfig` CoW value type.
- Convenience factory `singleLink` / `dualLink` / `quadLink`.
- `validate()` checks the standard's `cablesFor` against
  `ports().size()`.
- `PROMEKI_DATATYPE` + `DataTypeSdiSignalConfig = 0x65`.
- Doctest: every helper, every factory, every round-trip
  (DataStream / Variant / JSON / String), `validate()` accepts
  matching combinations and rejects mismatches.
- New `MediaConfig::SdiInputSignal` + `SdiOutputSignal` keys
  declared in `mediaconfig.h`.

### Phase 3 — `HdmiSpecVersion` + `HdmiSignalConfig`

- Same shape as Phase 2 for the HDMI family.
- `DataTypeHdmiSignalConfig = 0x66`.
- `MediaConfig::HdmiInputSignal` + `HdmiOutputSignal` keys.

### Phase 4 — `VideoReferenceConfig`

- `VideoReferenceSource` + `VideoReferenceRateFamily` `TypedEnum`s.
- `VideoReferenceConfig` CoW value type with the
  `FromSignal` parameterisation.
- `DataTypeVideoReferenceConfig = 0x67`.
- `MediaConfig::VideoReference` key.

### Phase 5 — Smoke wiring + integration verification

- One `MediaConfig::Type` consumer (the existing
  `TpgFactory::configSpecs` or similar) lists the new keys
  defensively so JSON config files exercising the new keys
  round-trip end-to-end through `mediaplay --config foo.json`.
  Goal: prove the new types are first-class through the existing
  config plumbing, even though no MediaIO backend acts on them
  yet (those come in [`ntv2.md`](ntv2.md)).
- `tests/func/video-carrier-roundtrip/` — promeki-test case that
  builds a `MediaConfig` carrying each new key, serialises to
  JSON, parses back, compares.

## Cross-cutting / library follow-ups

- The existing `HdmiInfoFrame` class (`include/promeki/hdmiinfoframe.h`)
  is an unrelated *ANC packet helper*.  Names are similar enough
  to confuse a fresh reader.  Add cross-referencing "see also"
  Doxygen blocks both ways when the new types land.
- `EnumList` natively holds a heterogeneous list of enum values;
  if a downstream consumer (e.g. a future "supported link
  standards" capability key) wants `EnumList<SdiLinkStandard>`,
  verify the EnumList path is happy with this enum — should be
  automatic via the TypedEnum registration but worth a doctest.

## Open questions

- **Core vs PROAV placement.** The four types describe *video*
  carriers, but they have no PROAV dependency (no PixelFormat /
  Image / VideoCodec touchpoints).  `VideoFormat` already lives
  in core for the same reason.  Placing the new types in core
  means tools that don't link PROAV (e.g. a future config
  validator CLI) can still parse / round-trip the MediaConfig
  keys.  Alternative: live in proav since they only matter to
  proav consumers in practice.  Lean toward core, but confirm
  before Phase 1.
- **`Auto` in toString.** The lowercase / snake_case mapping
  (`smpte12g2082`, `fromsignal`) is one option; a more verbose
  form (`SmpteSt2082-12G`, `FromSignal:Sdi1`) is another.
  Existing enum `toString` returns the canonical enum identifier
  unchanged (e.g. `"Smpte12G2082"`).  Decide the
  `SdiSignalConfig::toString` shape before Phase 2 codifies it.
- **`SdiLinkStandard::Auto` on sinks.** On a source, `Auto` means
  "detect what's arriving."  On a sink the standard is forced by
  the chosen `VideoFormat` + the device's reservable cable count.
  Either reject `Auto` on sinks at `validate()`, or define it as
  "infer from the offered `MediaDesc`" and document.  Probably
  the latter, but the rule lives on the SignalConfig type's
  contract, not the backend.
- **Sub-image mapping enum split?**  `SmpteQuadLink3G_SquareDiv`
  and `SmpteQuadLink3G_2SI` differ only in their sub-image
  mapping.  Splitting into a separate `SdiSubImageMapping`
  field on `SdiSignalConfig` would let the standard enum stay
  narrower (`SmpteQuadLink3G`) at the cost of two fields per
  config.  Current draft inlines the mapping into the standard
  enum because it's the path the AJA SDK takes too
  (`NTV2_VIDEO_FORMAT_4x1920x1080p_6000` already encodes the
  square-division choice in the format enum).  Revisit if a
  consumer wants Auto-detect for the mapping but not the
  carrier.
- **ST 2110 / ST 2022-6 sibling type.**  Network SDI carriers
  don't have a physical `VideoPortRef`, but they do still pick a
  link standard (the originating SDI signal's standard, embedded
  in the IP stream's media description).  Probably grows a
  sibling — `SmpteIpSignalConfig` — with an SDP-file or
  RTP-stream reference instead of a `VideoPortRef`.  Worth
  keeping `SdiSignalConfig` focused on **physical** SDI so
  there's room.  Out of scope for this milestone.
- **Embedding in `MediaDesc`?** A future enhancement is to let
  `MediaDesc` itself carry an `SdiSignalConfig` (rather than only
  on `MediaConfig` open-time keys) so a captured frame's
  descriptor records the link standard it arrived on.  Useful
  for inspector / pmdf debugging.  Not in this milestone; the
  per-key MediaConfig surface is enough to unblock the NTV2
  backend.

## Dependency on this milestone

- [`devplan/proav/ntv2.md`](ntv2.md) Phase 1 — requires all five
  types (the four carrier types + the matching MediaConfig keys).
- Future DeckLink backend — same shape, reuses unchanged.
- Future ST 2022-6 / ST 2110 backends — reuses
  `SdiLinkStandard` directly; needs the sibling
  `SmpteIpSignalConfig` (above) for the port-equivalent field.
- `mediaplay --probe` for any SDI-capable backend — surfaces
  `SdiSignalConfig` in its device-enumeration JSON output once
  available.

## Validation checklist

- `build` clean, warnings-free.
- `build check` passes (full doctest matrix).
- `Variant::dataType(v) == DataTypeOf<T>::id` for each new type.
- `DataStream(v).read(v2) && v == v2` for each new type and a
  handful of populated instances.
- `JSON::dump(v).parse() == v` for each new type.
- `String s = v.toString(); T::fromString(s) == v` for each new
  type.
- `mediaplay --dc SdiInputSignal:smptehd292:sdi1 ...` accepts the
  config key from the CLI and survives JSON round-trip.

## References

- `include/promeki/ancdesc.h` + `src/proav/ancdesc.cpp` — current
  reference impl for CoW value types with `PROMEKI_DATATYPE` +
  `PROMEKI_SHARED_FINAL` + DataStream + Variant integration.
- `include/promeki/datatype.h` — `DataTypeID` enum + the
  `PROMEKI_DATATYPE` macro definition.
- `src/core/datatype.cpp` — `registerBuiltinDataTypes()` where
  the new lines land.
- `include/promeki/enums.h` — `TypedEnum` + the
  `PROMEKI_REGISTER_ENUM_TYPE` macro for the carrier enums.
- `include/promeki/mediaconfig.h` — where the new `MediaConfig`
  keys land.
- [`devplan/proav/ntv2.md`](ntv2.md) — the consumer driving the
  initial requirement set.
