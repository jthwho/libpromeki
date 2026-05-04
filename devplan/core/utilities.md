# Enhanced Existing Classes

**Phase:** 7 (ongoing)
**Standards:** All code must follow `CODING_STANDARDS.md`. Every class requires complete doctest unit tests. See `README.md` for full requirements.

Cross-cutting enhancements to existing core classes (String, Variant,
Color, VariantDatabase, Metadata, TimeStamp, etc.). The historical
"shipped" feature catalogue (Variant template engine, `Url`, `Units`,
`Duration` formatter, `VariantList` / `VariantMap`, nested-key lookup,
`Variant::peek`, `VariantSpec::coerce`, `VariantLookup::variantTree`,
…) lives in git history. The items below are the remaining
opportunistic enhancements.

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


