# Enhanced Existing Classes

**Phase:** 7 (ongoing)
**Standards:** All code must follow `CODING_STANDARDS.md`. Every class requires complete doctest unit tests. See `README.md` for full requirements.

String, Variant, Color/ColorModel, StringRegistry/VariantDatabase/Config, Metadata, Size2DTemplate/TimeStamp, and the C++20 `std::format` integration are done. See git history for the completed work. The items below are the remaining opportunistic enhancements.

---

## Result\<T\> Adoption

Opportunistic migration: any `std::pair<T, Error>` return still in the codebase should be converted to `Result<T>` when the class is next touched. No new code should return `std::pair<T, Error>`.

---

## Variant Enhancements

Needed to make `MediaConfig`/`MediaPipelineConfig` JSON round-trip work across every type a pipeline config might carry.

- [ ] `HashMap<String, Variant>` type support (map variant)
- [ ] `List<Variant>` type support (list/array variant)
- [ ] `Buffer` type support
- [ ] `toJson()` — convert variant to `JsonObject`/`JsonArray`
- [ ] `fromJson(const JsonObject &)` — construct variant from JSON
- [ ] `fromJson(const JsonArray &)` — construct variant from JSON array
- [ ] Round-trip tests: Variant → JSON → Variant for every registered Variant type (`PixelDesc`, `MediaDesc`, `FrameRate`, `Color`, `SocketAddress`, `UUID`, `UMID`, etc.)
- [ ] Update `VariantImpl` template for new types

---

## String Enhancements

- [ ] `String::format` migration — migrate `String::sprintf` and `String::arg` call sites over time (neither is deprecated yet; just opportunistic)

---

## RegEx Enhancements

- [ ] `matchAll(const String &subject)` — returns `List` of all matches
- [ ] `captureGroups()` — returns `List<String>` of capture groups from last match
- [ ] Named capture support: `(?P<name>...)` syntax
- [ ] `namedCapture(const String &name)` — returns named capture from last match
- [ ] Update tests
