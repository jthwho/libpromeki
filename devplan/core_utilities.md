# Enhanced Existing Classes

**Phase:** 7 (ongoing)
**Standards:** All code must follow `CODING_STANDARDS.md`. Every class requires complete doctest unit tests. See `README.md` for full requirements.

---

### StringRegistry, VariantDatabase, Config (COMPLETE)

New utility classes providing a generic named-value store built on string interning:

- `StringRegistry<Tag>` — thread-safe append-only registry that maps unique `String` values to compact `uint32_t` IDs. Separate registries per tag type; IDs are process-local and must not be persisted.
- `VariantDatabase<Tag>` — maps `StringRegistry::Item` IDs to `Variant` values. Supports JSON, DataStream, and TextStream serialization. Per-tag ID namespace ensures independent databases cannot accidentally share IDs. `operator==`/`operator!=` added for value equality comparison.
- `Config` — convenience alias `VariantDatabase<ConfigTag>` for application configuration use.
- `Error::IdNotFound` and `Error::ConversionFailed` added for `VariantDatabase::getAs()` error reporting.

Tests: `tests/stringregistry.cpp`, `tests/variantdatabase.cpp`, `tests/configoption.cpp`.

---

### Metadata Refactored to VariantDatabase (COMPLETE)

`Metadata` now inherits from `VariantDatabase<MetadataTag>` instead of owning an internal `Map<ID, Variant>`. The old X-macro `PROMEKI_ENUM_METADATA_ID` enum and the static lookup tables have been removed. Well-known metadata keys are now `static inline const ID` members (e.g., `Metadata::Title`, `Metadata::FrameRate`). `idToString()` and `stringToID()` remain as thin wrappers delegating to `ID::name()` and `ID(val)` respectively. `fromJson()` is simplified — no per-key type dispatch needed. `operator==` delegates to `VariantDatabase::operator==`.

Tests: `tests/metadata.cpp` — all existing tests updated; equality tests added.

---

### Variant Equality (COMPLETE)

`VariantImpl::operator==`/`operator!=` added with three-tier comparison semantics:
1. Same type — direct `operator==`.
2. Cross-type arithmetic — promotes to `double` (if either is floating-point) or uses safe signed/unsigned `uint64_t` comparison with a negative-value guard.
3. Cross-type convertible — attempts `get<A>()` on the other side, then `get<B>()` on this side; equality if either conversion succeeds and values match.

`detail::VariantEnd` gains `operator==`/`operator!=` to satisfy the `std::visit` path for the sentinel type.

Tests: `tests/variant.cpp` — `Variant_EqualitySameType`, `Variant_InequalitySameType`, `Variant_EqualityCrossNumeric`, `Variant_EqualityCrossConvertible`, `Variant_EqualityInvalid`.

---

### Simple Data Object Equality Operators (COMPLETE)

`operator==`/`operator!=` added to:
- `Size2DTemplate<T>` (covers `Size2Du32`, `Size2Di32`, `Size2Df`, `Size2Dd`) — compares width and height.
- `TimeStamp` — compares the underlying `_value` (nanosecond epoch counter).

Tests: `tests/size2d.cpp` and `tests/timestamp.cpp` — equality test cases added for both.

---

### Result\<T\> Adoption

Migrate existing `std::pair<T, Error>` returns to `Result<T>` as classes are touched:

- [ ] `Queue<T>::pop()` — `Result<T>`
- [ ] Existing codebase: migrate `std::pair<T, Error>` returns (e.g., `Timecode::fromString()`) to `Result<T>`

---

### Variant Enhancements

- [ ] Add `HashMap<String, Variant>` type support (map variant)
- [ ] Add `List<Variant>` type support (list/array variant)
- [ ] Add `Buffer` type support
- [ ] `toJson()` — convert variant to `JsonObject`/`JsonArray`
- [ ] `fromJson(const JsonObject &)` — construct variant from JSON
- [ ] `fromJson(const JsonArray &)` — construct variant from JSON array
- [ ] Round-trip tests: Variant -> JSON -> Variant
- [ ] Update `VariantImpl` template for new types
- [ ] Update tests

---

### String Enhancements

- [ ] `arg(const String &)` — Qt-style `String("Hello %1").arg(name)`, replaces lowest numbered `%N` placeholder
- [ ] `arg(int)`, `arg(double)` — numeric overloads with optional format/precision
- [ ] `number(int)` — static factory, returns String representation
- [ ] `number(double, int precision = 6)` — static factory
- [ ] `toInt(Error *err = nullptr)` — parse to int
- [ ] `toDouble(Error *err = nullptr)` — parse to double
- [ ] `toFloat(Error *err = nullptr)` — parse to float
- [ ] `toInt64(Error *err = nullptr)` — parse to int64_t
- [ ] Update tests for all new String methods

---

### RegEx Enhancements

- [ ] `matchAll(const String &subject)` — returns `List` of all matches
- [ ] `captureGroups()` — returns `List<String>` of capture groups from last match
- [ ] Named capture support: `(?P<name>...)` syntax
- [ ] `namedCapture(const String &name)` — returns named capture from last match
- [ ] Update tests
