# Enhanced Existing Classes

**Phase:** 7 (ongoing)
**Standards:** All code must follow `CODING_STANDARDS.md`. Every class requires complete doctest unit tests. See `README.md` for full requirements.

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
