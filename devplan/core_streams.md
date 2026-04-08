# Core Streams and Serialization

**Phase:** 2 — **COMPLETE**
**Dependencies:** Phase 1 (containers), Phase 2A (IODevice)
**Depends-on-this:** Phase 4 (pipeline frame serialization), Phase 7 (ObjectBase saveState/loadState)
**Standards:** All code must follow `CODING_STANDARDS.md`. Every class requires complete doctest unit tests. See `README.md` for full requirements.

Provides promeki-native stream classes for binary and text I/O. Qt-style API, integrates with IODevice and promeki types. DataStream, TextStream, and StreamString are all implemented. All std:: stream usage has been migrated out of the library (only `tests/doctest_main.cpp` retains std:: streams, required by the doctest API).

The remaining sections in this document are **extension work** — adding type-specific stream operators and ObjectBase serialization.

---

## Phase 2K: TextStream Promeki Type Operator Extensions

**Dependencies:** TextStream (complete)

Add free `operator<<(TextStream &, const T &)` and `operator>>(TextStream &, T &)` overloads so promeki types can be written/read with TextStream directly (e.g., `stream << myPoint << myTimecode`). Each overload lives in the type's own header, forward-declaring `TextStream` to avoid circular includes (similar to how Qt places `QTextStream` operators alongside each type). See `CODING_STANDARDS.md` for the full convention.

- [ ] `Point<T, N>` — e.g. `"(1, 2, 3)"`
- [ ] `Size2DTemplate<T>` — e.g. `"1920x1080"`
- [ ] `Rect<T>` — e.g. `"(x, y, w, h)"`
- [ ] `Rational<T>` — e.g. `"24000/1001"`
- [ ] `UUID` — string representation
- [ ] `Timecode` — SMPTE string
- [ ] `TimeStamp` — formatted representation
- [ ] `DateTime` — ISO 8601 or similar
- [ ] `FourCC` — four-character string
- [ ] `Color` — RGBA components
- [ ] `XYZColor` — X, Y, Z components
- [ ] `AudioDesc` — human-readable summary
- [ ] `ImageDesc` — human-readable summary
- [ ] `VideoDesc` — human-readable summary
- [ ] `Metadata` — key-value dump
- [ ] `Error` — name and description
- [ ] `Duration` — formatted representation
- [ ] `FrameRate` — string representation
- [ ] `StringList` — delimited output
- [ ] `List<T>` — bracketed, comma-separated (where T is streamable)
- [ ] `Map<K,V>` — bracketed key-value pairs
- [ ] `Set<T>` — bracketed, comma-separated

---

## Phase 2L: DataStream Promeki Type Serialization Extensions — **COMPLETE**

**Dependencies:** DataStream (complete)
**Depends-on-this:** Phase 4 (pipeline frame serialization), Phase 7 (ObjectBase saveState/loadState)

`DataStream` operators for binary serialization of promeki types. Unlike the TextStream plan, most operators live in `datastream.h` itself (as member functions for concrete types or free templates for container/geometry families), because datastream.h already pulls in the core types transitively via `variant.h`. The Shareable data objects (AudioDesc, ImageDesc, MediaDesc) put their operators in their own headers to avoid bloating datastream.h with media dependencies.

- [x] `Point<T, N>` — tag + `uint32_t` dims + N tagged elements (datastream.h, template)
- [x] `Size2DTemplate<T>` — tag + tagged width + tagged height (datastream.h, template)
- [x] `Rect<T>` — tag + tagged x/y/w/h (datastream.h, template)
- [x] `Rational<T>` — tag + tagged numerator/denominator (datastream.h, template)
- [x] `UUID` — tag + 16 raw bytes (datastream.h)
- [x] `Timecode` — tag + length-prefixed canonical string (datastream.h)
- [x] `TimeStamp` — tag + `int64_t` nanoseconds since epoch (datastream.h)
- [x] `DateTime` — tag + `int64_t` nanoseconds since epoch (datastream.h)
- [x] `Color` — tag + length-prefixed lossless ModelFormat string (datastream.h)
- [x] `XYZColor` — tag + three tagged doubles (datastream.h — defined here to avoid a cycle with colormodel → ciepoint → xyzcolor)
- [x] `ColorModel` / `MemSpace` / `PixelFormat` / `PixelDesc` / `FrameRate` / `Enum` / `StringList` (datastream.h)
- [x] `AudioDesc` — tag + dataType + sampleRate + channels + metadata (audiodesc.h)
- [x] `ImageDesc` — tag + size + pixelDesc + linePad + lineAlign + interlaced + metadata (imagedesc.h)
- [x] `MediaDesc` — tag + frameRate + imageList + audioList + metadata (mediadesc.h)
- [x] `Metadata` — inherited via `VariantDatabase<Tag>` template operators in variantdatabase.h
- [x] `List<T>` — tag + tagged `uint32_t` count + N tagged elements (datastream.h, template)
- [x] `Map<K,V>` — tag + count + key/value pairs (datastream.h, template)
- [x] `Set<T>` — tag + count + elements (datastream.h, template)
- [x] `HashMap<K,V>` — tag + count + key/value pairs (datastream.h, template)
- [x] `HashSet<T>` — tag + count + elements (datastream.h, template)
- [x] `Buffer::Ptr` — shares `TypeBuffer` tag with `Buffer`; nullable (datastream.h)
- [x] `JsonObject` / `JsonArray` — tag + length-prefixed compact JSON text (json.h)

**Note:** `VideoDesc` was renamed to `MediaDesc` during the MediaIO refactor. MediaDesc is what ships.

### Wire-format direct/Variant symmetry

Writing a `Variant` that currently holds a `UUID` emits the same bytes as writing a direct `UUID`. The old outer `TypeVariant` wrapper tag is gone: a `Variant` read peeks whatever tag is next and dispatches. This means readers can flip between direct and `Variant` forms freely, which is the main reason Phase 7's `saveState`/`loadState` will be straightforward.

### Extensibility — **COMPLETE**
- [x] Document pattern for user types in `datastream.h` (`@par Extension example`). Pattern: call `writeTag(id)` to frame your data, `readTag(id)` to validate on read, and `setError()` to report meaningful errors. `readTag`, `writeTag`, and `setError` are the public extension API. Any type with these operators automatically participates in the container templates (`List<Waypoint>`, `Map<String, Waypoint>`, etc.) and the `read<T>()` Result-returning API.

### Result<T> read API
- [x] `template<T> Result<T> DataStream::read()` — bridges stream state with the library's standard `Result<T>` convention.

### Better error reporting
- [x] `errorContext()` — descriptive string set at every failure site ("expected tag 0x07, got 0x02", "short read: wanted 4 bytes, got 0", etc.).
- [x] `toError()` — maps stream Status to `Error` codes (Ok / EndOfFile / CorruptData / IOError).
- [x] `setError()` preserves the first failure (no overwrite).

### Golden wire-format tests
- [x] `DataStream golden: ...` test block in `tests/datastream.cpp` pins the exact byte layout for primitives, String, UUID, Size2D, Rational, Invalid Variant, List, and MemSpace. Any future wire-format drift trips these tests immediately.

---

## ObjectBase Serialization (saveState / loadState)

**Phase:** 7 (enhanced existing classes) — depends on DataStream
**Dependencies:** DataStream (complete)

Add binary state serialization to ObjectBase. Each subclass can override to save/restore its own state. Uses DataStream for portable, versioned binary format.

**Files:**
- [ ] Modify `include/promeki/objectbase.h`
- [ ] Modify `src/core/objectbase.cpp`
- [ ] `tests/objectbase_serialization.cpp`

**Implementation checklist:**
- [ ] `virtual Error saveState(DataStream &stream) const` — default: saves nothing, returns Ok
- [ ] `virtual Error loadState(DataStream &stream)` — default: reads nothing, returns Ok
- [ ] `Error saveState(Buffer &buffer) const` — convenience: creates DataStream over BufferIODevice
- [ ] `Error loadState(const Buffer &buffer)` — convenience: creates DataStream over BufferIODevice
- [ ] Convention: each class writes a version tag first, then its data
  - [ ] `stream << (uint16_t)1; // version`
  - [ ] On load: read version, handle backward compat
- [ ] Subclass pattern:
  ```
  Error MyClass::saveState(DataStream &stream) const {
      Error err = ObjectBase::saveState(stream);  // save parent state first
      if(err) return err;
      stream << (uint16_t)1;  // MyClass version
      stream << _myField1 << _myField2;
      return stream.status() == DataStream::Ok ? Error() : Error("write failed");
  }
  ```
- [ ] `PROMEKI_SIGNAL(stateLoaded)` — emitted after successful loadState
- [ ] `PROMEKI_SIGNAL(stateSaved)` — emitted after successful saveState (optional, for debugging)
- [ ] Recursive save/load: option to save/load child object states
  - [ ] `Error saveStateRecursive(DataStream &stream) const` — saves self + all children
  - [ ] `Error loadStateRecursive(DataStream &stream)` — loads self + all children
  - [ ] Children saved in order, with count prefix
- [ ] Doctest: save/load a test ObjectBase subclass, verify round-trip, test versioning, test recursive save/load

---

## Design Notes

### DataStream wire format (v1)

**Header — fixed 16 bytes:**
| Offset | Size | Field |
|---|---|---|
| 0-3 | 4 | Magic bytes `"PMDS"` (`0x50 0x4D 0x44 0x53`) |
| 4-5 | 2 | uint16_t version, always big-endian. Current: 1. |
| 6   | 1 | Byte-order marker: `'B'` (0x42) or `'L'` (0x4C) |
| 7-15 | 9 | Reserved, **must be zero**. Readers validate — any non-zero byte is `ReadCorruptData`. |

The byte-order marker is written by `createWriter(device, order)` and read by `createReader(device)`, so writers and readers do not need to agree out-of-band. The 9 reserved bytes give future versions room to add optional header fields; non-zero values are rejected so a future reader can tell "this stream has features I don't understand" immediately instead of silently mis-parsing.

**Payload — tagged values:**
- Every value is preceded by a one-byte `TypeId` tag. On read, the tag is validated against the expected type; mismatches set status to `ReadCorruptData` with a descriptive `errorContext()`.
- Tag ranges:
  - `0x01-0x0D`: primitives (int8..double, bool, String, Buffer)
  - `0x0E`: `TypeInvalid` (explicit invalid marker, empty payload)
  - `0x10-0x1F`: data objects (UUID, DateTime, TimeStamp, Size2D, Rational, FrameRate, Timecode, Color, ColorModel, MemSpace, PixelFormat, PixelDesc, Enum, StringList, Rect, Point)
  - `0x20-0x24`: containers (List, Map, Set, HashMap, HashSet)
  - `0x30-0x35`: shareable types (JsonObject, JsonArray, XYZColor, AudioDesc, ImageDesc, MediaDesc)
  - `0x80-0xFF`: reserved for user types (see datastream.h extension example)
- Multi-byte integers: byte-order controlled by the header marker.
- Strings: `uint32_t` length prefix (byte count) + UTF-8 bytes (no NUL terminator). Latin1 input with non-ASCII is auto-converted to UTF-8.
- Buffers: `uint32_t` length prefix + raw bytes.
- Floats/doubles: IEEE 754, byte-swapped if needed.
- Variants: no outer wrapper — write the value with its normal tag, and the Variant reader peeks whatever tag is there and dispatches. Writing `Variant{UUID}` is byte-identical to writing a direct `UUID`.
- Size2D, Rect, Point, Rational: inner primitive values carry their own tags. This lets one template operator cover all element types (e.g. `Size2D<int32_t>` vs `Size2D<uint32_t>`) and lets the reader catch element-type mismatches via normal tag validation.
- StringList, List, Map, Set, HashMap, HashSet: outer tag + `TypeUInt32`-tagged count + N fully-tagged elements.
- 256 MiB sanity limit on container and length-prefixed reads.
- Raw byte methods (`readRawData`, `writeRawData`, `skipRawData`) are untagged — they're the escape hatch for embedding binary payloads that aren't self-describing.

### Error reporting
- `Status` enum remains for categorization (Ok / ReadPastEnd / ReadCorruptData / WriteFailed).
- `errorContext()` returns a human-readable string set at the failure site.
- `toError()` maps to the library's standard `Error` codes for interop.
- First error wins — `setError()` is a no-op once the stream is in a non-Ok state. Call `resetStatus()` to recover.

### Extension API
Public: `writeTag`, `readTag`, `setError`. User operators use these to frame their data and report errors the same way built-ins do; see the worked example in the `datastream.h` class doc.

### TextStream design
TextStream is an independent class (does not inherit from `std::ostream`). The formatting controls and encoding-awareness are different enough from `std::ostream` that inheritance creates more friction than value. This means all `operator<<`/`operator>>` overloads must be provided for TextStream explicitly. These overloads live in each type's own header (forward-declaring `TextStream`), not in `textstream.h`, following the Qt convention. See `CODING_STANDARDS.md` for the full convention.
