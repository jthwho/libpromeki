# Enhanced Existing Classes

**Phase:** 7 (ongoing)
**Standards:** All code must follow `CODING_STANDARDS.md`. Every class requires complete doctest unit tests. See `README.md` for full requirements.

Cross-cutting enhancements to existing core classes (String, Variant,
Color, VariantDatabase, Metadata, TimeStamp, etc.). The historical
"shipped" feature catalogue (Variant template engine, `Url`, `Units`,
`Duration` formatter, `VariantList` / `VariantMap`, nested-key lookup,
`Variant::peek`, `VariantSpec::coerce`, `VariantLookup::variantTree`,
`Function<Sig>`, `Optional<T>`, …) lives in git history. The items below
are the remaining opportunistic enhancements.

## FilePath / FileInfo symlink and pseudo-symlink API (2026-05-26)

`FilePath` and `FileInfo` gained a pair of "link" APIs that abstract
over both OS symlinks and *pseudo-symlinks* (magic-header text files used
on filesystems / platforms that don't support real symlinks):

- `FilePath::isSymlink()` — OS symlink check via `symlink_status`.
- `FilePath::isPseudoSymlink()` — reads the file body; validates magic
  header (`#!/promeki/symlink`), size guard (≤ 4096 bytes), and payload
  (non-empty, no NUL / control chars).
- `FilePath::isLink()` — either kind.
- `FilePath::readSymlink()` / `readPseudoSymlink()` / `readLink()` —
  returns the stored target verbatim.
- `FilePath::writePseudoSymlink(target)` — creates / overwrites a
  pseudo-symlink file.
- `FilePath::resolveLink(maxHops=16)` — follows a chain of OS + pseudo
  symlinks to the final target, with loop detection and relative-path
  resolution at each hop.
- `FilePath::kPseudoSymlinkMagic` / `kPseudoSymlinkMaxBytes` — constants
  for tooling that needs to write or validate pseudo-symlink files without
  going through the API.
- `FileInfo::isSymlink()` / `isPseudoSymlink()` / `isLink()` — delegating
  wrappers for callers who already hold a `FileInfo`.

Implementation lives in `src/core/filepath.cpp` (new file).
Doctest coverage: `tests/unit/filepath.cpp` (18 link-API cases) and
`tests/unit/fileinfo.cpp` (3 link-API cases).

---

## Result\<T\> Adoption

Opportunistic migration: any `std::pair<T, Error>` return still in the codebase should be converted to `Result<T>` when the class is next touched. No new code should return `std::pair<T, Error>`.

**2026-05-15**: `fromString` convention standardization complete. UUID, UMID, FrameNumber, FrameCount, MediaDuration, DateTime, Color, and all typereg wrappers (ColorModel, PixelMemLayout, PixelFormat, AncFormat) plus VariantList, VariantMap, Enum now all expose `static Result<T> fromString(const String &)`. The `Detail::HasResultFromString` concept in `datatype.h` auto-populates the DataType registry's `ops.fromString` slot for any conforming type.

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


