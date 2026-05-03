# Enhanced Existing Classes

**Phase:** 7 (ongoing)
**Standards:** All code must follow `CODING_STANDARDS.md`. Every class requires complete doctest unit tests. See `README.md` for full requirements.

String, Variant, Color/ColorModel, StringRegistry/VariantDatabase/Config, Metadata, Size2DTemplate/TimeStamp, and the C++20 `std::format` integration are done. `SdpSession` was added as a Variant type (`TypeSdpSession`) with String-to-SdpSession conversion via `SdpSession::fromString()`. TimeStamp gained `promeki::Duration` interop operators (`+=`, `-=`, `+`, `-`, and `durationBetween`). `Variant::format(spec)` dispatches a format spec string to the held type's `std::formatter` specialization (or falls back to string-form formatting). `VariantDatabase::format(tmpl, resolver, err)` provides `{Key[:spec]}` template string substitution with `{{`/`}}` escapes and optional fallback resolver for nested resolution chains. `VariantDatabase::setFromJson()` gained spec-driven coercion of JSON strings to native Variant types via `VariantSpec::parseString()`. `VideoFormat` registered as `Variant::TypeVideoFormat`. `Units` class (`include/promeki/units.h`) centralizes all metric/IEC-binary unit formatting — `fromByteCount`, `fromDuration`, `fromDurationNs`, `fromFrequency`, `fromSampleCount`, `fromBytesPerSec`, `fromBitsPerSec`, `fromItemsPerSec` — replacing `String::fromByteCount` and the ad-hoc helpers in benchmark code. `Duration` gained `toScaledString()` delegating to `Units::fromDurationNs`, and `std::formatter<Duration>` was rewritten to support `{:hms}` (default) and `{:scaled}` format specs with standard width/fill/alignment sub-specs. `Url` class added (`include/promeki/url.h`, `src/core/url.cpp`) with RFC 3986 parser/serializer, registered as `Variant::TypeUrl`; `VariantSpec::parseString` and `DataStream` extended accordingly; see Phase 4r in `README.md`. **`VariantList`** (`TypeVariantList`) and **`VariantMap`** (`TypeVariantMap`) landed as first-class pimpl-backed Variant alternatives (see Variant Enhancements below); `DataStream` wire tags added, `Variant::fromJson` fills them from JSON arrays/objects, and `VariantDatabase::setFromJson` now performs spec-coercion-aware recursive descent into nested VariantList/VariantMap values. `VariantSpec` gained `setElementSpec`/`setValueSpec` (with `elementSpec`/`valueSpec`/`hasElementSpec`/`hasValueSpec`) and a `coerce()` entry-point for full recursive spec-driven coercion, plus an explicit copy constructor and copy-assignment operator. `VariantDatabase::format()` now supports nested-key resolution (`"Foo.bar"`, `"Foo[0]"`). `Variant::peek<T>()` added for cheap non-copying borrow of a held alternative. `promekiResolveVariantPath()` provides a standalone path-walker over nested VariantMap/VariantList trees. `VariantLookup::Registrar::variantTree()` adds a new composition handler that resolves trailing path segments through an arbitrary VariantList/VariantMap tree held by an instance. `util.h` `promekiConvert` fixed to use `std::cmp_less`/`std::cmp_greater` for safe signed/unsigned boundary comparisons. See git history for the completed work. The items below are the remaining opportunistic enhancements.

---

## Result\<T\> Adoption

Opportunistic migration: any `std::pair<T, Error>` return still in the codebase should be converted to `Result<T>` when the class is next touched. No new code should return `std::pair<T, Error>`.

---

## Variant Enhancements

Needed to make `MediaConfig`/`MediaPipelineConfig` JSON round-trip work across every type a pipeline config might carry.

- [x] `Map<String, Variant>` type support (map variant) — `VariantMap` (`TypeVariantMap`), pimpl-backed
- [x] `List<Variant>` type support (list/array variant) — `VariantList` (`TypeVariantList`), pimpl-backed
- [ ] `Buffer` type support
- [x] `fromJson(const JsonObject &)` — `Variant::fromJson` fills `VariantMap` from JSON objects (recursive)
- [x] `fromJson(const JsonArray &)` — `Variant::fromJson` fills `VariantList` from JSON arrays (recursive)
- [ ] `toJson()` — convert `VariantMap`/`VariantList` back to `JsonObject`/`JsonArray` (toJsonString only for now)
- [ ] Round-trip tests: Variant → JSON → Variant for every registered Variant type (`PixelFormat`, `MediaDesc`, `FrameRate`, `Color`, `SocketAddress`, `UUID`, `UMID`, etc.)
- [x] `VariantImpl` template updated for `TypeVariantList` / `TypeVariantMap` (`DataStream` wire tags added)

---

## RegEx Enhancements

- [ ] `matchAll(const String &subject)` — returns `List` of all matches
- [ ] `captureGroups()` — returns `List<String>` of capture groups from last match
- [ ] Named capture support: `(?P<name>...)` syntax
- [ ] `namedCapture(const String &name)` — returns named capture from last match
- [ ] Update tests

---


