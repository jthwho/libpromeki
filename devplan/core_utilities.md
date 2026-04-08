# Enhanced Existing Classes

**Phase:** 7 (ongoing)
**Standards:** All code must follow `CODING_STANDARDS.md`. Every class requires complete doctest unit tests. See `README.md` for full requirements.

**Completed:** StringRegistry/VariantDatabase/Config, Metadata refactor to VariantDatabase, Variant TypeRegistry type support (ColorModel/MemSpace/PixelFormat/PixelDesc), Variant equality operators, Size2DTemplate/TimeStamp equality operators, Color/ColorModel refactoring (float[4] + ColorModel, 19 color models, 2187 test assertions). See git history for details.

---

### Result\<T\> Adoption

Migrate existing `std::pair<T, Error>` returns to `Result<T>` as classes are touched:

**Completed in String/Timecode/MusicalScale/Queue/Set overhaul:**
- `Queue<T>::pop()` and `Queue<T>::peek()` — `Result<T>` ✓
- `Timecode::fromString()` — `Result<Timecode>` ✓
- `Timecode::toString()` — `Result<String>` ✓
- `Timecode::toFrameNumber()` — `Result<FrameNumber>` ✓
- `MusicalScale::fromName()` — `Result<MusicalScale>` ✓
- `MusicalScale::modeFromName()` — `Result<MusicalScale::Mode>` ✓
- `Set::insert()` — `Pair<Iterator, bool>` ✓
- `Pair` constructor rewritten with perfect-forwarding (supports move-only types like `unique_ptr`) ✓
- All callers updated from `.first`/`.second` struct members to `.first()`/`.second()` methods ✓

Remaining `std::pair<T, Error>` returns in the codebase should be migrated opportunistically as classes are touched.

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

**Completed:** `std::formatter<VariantImpl<Types...>>` specialization — any Variant can be passed directly to `String::format` (delegates to `get<String>()`). ✓

---

### String Enhancements

**Completed (previous work):**
- `arg(const String &)` — Qt-style `String("Hello %1").arg(name)` ✓
- `arg(int)`, `arg(double)` — numeric overloads ✓
- `number(int/uint/int64/uint64/float/double/bool)` — static factories ✓
- `toInt(Error *err)`, `toUInt()`, `toDouble()`, `toFloat()`, `to<T>()` — parse overloads ✓

**Completed in String deep audit (this changeset):**
- Cross-encoding `find`/`rfind`/`count` — Latin1 haystack + Unicode needle was broken (UTF-8 bytes searched instead of codepoints) ✓
- Encoding-agnostic hash via `fnv1aMixCodepoint`/`fnv1aCodepoints`/`fnv1aLatin1AsCodepoints` — Latin1 and Unicode storage of the same logical content now hash identically, fixing `std::unordered_map<String, T>` across encodings ✓
- `operator==(const char *)` now decodes its argument as UTF-8 ✓
- `toUpper()`/`toLower()` — locale-independent, routes both Latin1 and Unicode through `Char::toUpper`/`Char::toLower` (fixes high-byte Latin1 case folding) ✓
- `operator<` total-order fix — was doing raw byte compare across encodings, breaking `std::map`/`std::set`; now compares codepoint-by-codepoint ✓
- `find(const char*)`, `rfind(const char*)`, `contains(const char*)` C-string overloads now decode as UTF-8 ✓
- `createSubstr` Latin1 narrowing optimization — Unicode substrings that contain only Latin1 codepoints now return Latin1 storage ✓
- `fromUtf8` ASCII fast path — pure-ASCII input returns cheap Latin1 storage ✓

**Completed: C++20 `std::format` integration:**
- `String::format(fmt, args...)` — compile-time checked wrapper around `std::format` returning `String` ✓
- `String::vformat(fmt, args)` — runtime-format-string variant ✓
- `std::formatter<promeki::String>` — inherits `std::formatter<string_view>`, all standard specs work ✓
- `std::formatter<promeki::Char>` — formats as UTF-8 byte sequence ✓
- `ToStringFormatter<T>` helper template + `PROMEKI_FORMAT_VIA_TOSTRING` macro ✓
- Macro applied to: UUID, FilePath, Enum, AudioDesc, NetworkAddress, Ipv4Address, Ipv6Address, AudioLevel, MacAddress, Duration, FrameRate, SocketAddress, ImageDesc, EncodedDesc, TimeStamp, XYZColor, SdpSession, JsonObject, JsonArray, DateTime, Color ✓
- Partial specializations for templates: `Rational<T>`, `Size2DTemplate<T>`, `Point<T,N>` ✓
- Bespoke `std::formatter<Timecode>` with custom format hint syntax (`smpte`/`smpte-fps`/`smpte-space`/`field` plus standard string specs) ✓

Remaining:
- [ ] `String::format` migration: migrate `String::sprintf` and `String::arg` call sites over time (neither is deprecated yet)

---

### RegEx Enhancements

- [ ] `matchAll(const String &subject)` — returns `List` of all matches
- [ ] `captureGroups()` — returns `List<String>` of capture groups from last match
- [ ] Named capture support: `(?P<name>...)` syntax
- [ ] `namedCapture(const String &name)` — returns named capture from last match
- [ ] Update tests
