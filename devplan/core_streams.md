# Core Streams and Serialization

**Phase:** 2 (alongside IO abstractions)
**Dependencies:** Phase 1 (containers), Phase 2A (IODevice)
**Depends-on-this:** Phase 4 (pipeline frame serialization), Phase 7 (ObjectBase saveState/loadState)
**Standards:** All code must follow `CODING_STANDARDS.md`. Every class requires complete doctest unit tests. See `README.md` for full requirements.

**Maintenance note:** Completed items are removed from this document once merged. Only retain completed items when they provide context needed by a future phase in this same document. If something is done, trust the code and git history as the source of truth.

Provides promeki-native stream classes for binary and text I/O. These present a Qt-style API and integrate with IODevice and promeki types. Once DataStream and TextStream are in place, all existing naked `std::` stream usage is migrated to use promeki streams.

---

## Completed

- **DataStream** — Binary stream for structured, portable serialization over IODevice. Wire format: 4-byte magic ("PMDS") + uint16_t version header; per-value TypeId tags; byte-order-aware encoding. Supports all primitives, String (UTF-8), Buffer, Variant (including DateTime, UUID, Timecode via string representation). Factory methods: `createWriter()`, `createReader()`. Raw byte access via `readRawData`/`writeRawData`/`skipRawData`. (`datastream.h/cpp`, `tests/datastream.cpp`)
- **BufferIODevice** — IODevice subclass backed by a `Buffer*`. Seekable, supports read/write, grows `size()` on writes up to `availSize()`. Does not own the buffer. (`bufferiodevice.h/cpp`, `tests/bufferiodevice.cpp`)
- **TextStream** — Formatted text I/O with encoding awareness (UTF-8, Latin-1). Constructors from `IODevice*`, `Buffer*`, `String*`, `FILE*`. Full set of `operator<<`/`operator>>` for primitives, String, Variant. Formatting controls: field width/alignment/pad char, integer base (dec/hex/oct/bin), float notation (fixed/scientific/smart), precision. Manipulator functions: endl, flush, hex, dec, oct, bin, fixed, scientific, left, right, center. Status tracking (Ok, ReadPastEnd, WriteFailed). readLine(), readAll(), read(maxLength), atEnd(). (`textstream.h/cpp`, `tests/textstream.cpp`)

---

## DataStream: Promeki Type Serialization Extensions

Once more promeki types need binary serialization (e.g. for pipeline frame serialization in Phase 4 or ObjectBase saveState in Phase 7), add `DataStream` operators. These can live in each type's header or in `datastream.h` — whichever avoids circular includes.

- [ ] `Point<T, N>` — N values of type T
- [ ] `Size2DTemplate<T>` — width, height
- [ ] `Rect<T>` — x, y, width, height
- [ ] `Rational<T>` — numerator, denominator
- [ ] `UUID` — 16 bytes (DataFormat)
- [ ] `Timecode` — frame number + mode identifier
- [ ] `TimeStamp` — internal representation
- [ ] `DateTime` — epoch + timezone info
- [ ] `Color` — RGBA components
- [ ] `XYZColor` — X, Y, Z components
- [ ] `AudioDesc` — sample rate, format, channels, etc.
- [ ] `ImageDesc` — width, height, pixel format, etc.
- [ ] `VideoDesc` — image desc + frame rate
- [ ] `Metadata` — serialize as key-value pairs
- [ ] `List<T>` — count + elements (where T is streamable)
- [ ] `Map<K,V>` — count + key-value pairs
- [ ] `Set<T>` — count + elements
- [ ] `HashMap<K,V>` — count + key-value pairs
- [ ] `HashSet<T>` — count + elements

### Extensibility
- [ ] Document pattern for user types: implement `operator<<(DataStream &, const MyType &)` and `operator>>(DataStream &, MyType &)`

---

## ObjectBase Serialization (saveState / loadState)

**Phase:** 7 (enhanced existing classes) — depends on DataStream
**Dependencies:** DataStream (complete)

Add binary state serialization to ObjectBase. Each subclass can override to save/restore its own state. Uses DataStream for portable, versioned binary format.

**Files:**
- [ ] Modify `include/promeki/core/core/objectbase.h`
- [ ] Modify `src/objectbase.cpp`
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

## Migration: Remove Naked std:: Stream Usage

Once TextStream is implemented, migrate all existing `std::` stream usage to promeki streams. This eliminates direct `std::` stream dependencies from the public API and internal implementation.

### Completed

- **AnsiStream Refactor** — Removed `std::ostream` inheritance; now composes `IODevice*` internally. Constructor takes `IODevice*`. `getCursorPosition()` takes `IODevice*`. All std:: stream includes removed. Tests use `StringIODevice`. (`ansistream.h/cpp`, `tests/ansistream.cpp`)
- **Logger Refactor** — Replaced `std::ofstream` with `FileIODevice*`, `std::cout` with `FileIODevice::stdoutDevice()`, `std::endl` with `"\n"` + `flush()`. Removed `<iostream>` and `<fstream>`. Tests use `File` + `TextStream`. (`logger.h/cpp`, `tests/logger.cpp`)
- **operator<< / operator>> Removal** — Removed `std::ostream`/`std::istream` stream operators and replaced internal std:: stream usage across: Point (operators removed, `fromString()`/`toString()` use `StringList::split()` + `String::dec()`), Size2D (operators removed), Rect (operator removed), Matrix3x3 (operator + `<ostream>` removed), Rational (operator removed, `toString()` uses `String::dec()`), FourCC (`<iostream>` removed).
- **String Internal Migration** — `dec()`/`hex()` use `String::number()`, `to<>()` uses `strtoll`/`strtoull`/`strtod`, `parseNumberWords()` uses `StringList::split()`, `floatToString()` uses `snprintf`. Removed `<iostream>` and `<sstream>`.
- **Internal std::stringstream Replacement** — XYZColor `toString()` uses `String::sprintf()` (`<sstream>` removed). DateTime `fromString()` uses `strptime()`, `fromNow()` uses `StringList::split()` + `strtoll` (`<sstream>` removed). JSON `<sstream>` removed. System `<iostream>` removed. MemPool `<iostream>` removed.
- **TUI Stream Migration** — `tui/application.cpp` uses `Application::stdoutDevice()` for AnsiStream. `tui/screen.cpp` uses `stream.flush()`. `tests/tui/screen.cpp` uses `StringIODevice`.
- **Utils Migration** — `promeki-info/main.cpp` uses `std::printf()` instead of `std::cout`/`std::endl`.
- **Test File Migration** — `tests/size2d.cpp`, `tests/matrix3x3.cpp`, `tests/ansistream.cpp`, `tests/logger.cpp`, `tests/string.cpp`, `tests/tui/screen.cpp`, `tests/dir.cpp` all migrated from std:: streams.

---

### StreamString Refactor

Currently extends `std::streambuf`. Refactor to work with `TextStream`. This is the last remaining std:: stream usage.

**Files:**
- [ ] Modify `include/promeki/core/core/streamstring.h`
- [ ] Update `tests/streamstring.cpp`

**Checklist:**
- [ ] Remove `std::streambuf` inheritance
- [ ] Internal: accumulate into `String` buffer directly (keep newline callback behavior)
- [ ] `stream()` method: return `TextStream &` instead of `std::ostream &`
- [ ] Remove `#include <streambuf>` and `#include <ostream>`
- [ ] Update tests: replace `std::flush` / `std::endl` with TextStream equivalents

---

### Verification

- [ ] `grep -r 'std::ostream\|std::istream\|std::iostream\|std::streambuf\|std::stringstream\|std::istringstream\|std::ostringstream\|std::ifstream\|std::ofstream\|std::fstream\|std::cout\|std::cerr\|std::cin\|std::endl\|std::flush' include/ src/ tests/ utils/ demos/` returns zero hits (excluding thirdparty)
- [ ] All tests pass after migration
- [ ] No `#include <iostream>`, `<ostream>`, `<istream>`, `<sstream>`, `<fstream>`, `<streambuf>` in non-thirdparty code

---

## Design Notes

### DataStream wire format
- Stream header: 4-byte magic (`0x50 0x4D 0x44 0x53` / "PMDS") + uint16_t version (big-endian). Current version: 1.
- Each value preceded by a one-byte TypeId tag (0x01–0x0E) for type validation on read.
- All multi-byte integers: byte-order controlled by `setByteOrder()`, default big-endian.
- Strings: `uint32_t` length prefix (byte count) + UTF-8 bytes (no null terminator). Latin1 with non-ASCII auto-converted to UTF-8.
- Buffers: `uint32_t` length prefix + raw bytes.
- Variants: TypeVariant tag + Variant::Type byte + untagged value. Complex types (DateTime, UUID, Timecode, etc.) serialized as String representation.
- Floats/doubles: IEEE 754, byte-swapped if needed.
- 256 MB sanity limit on String/Buffer reads.
- Raw byte methods (readRawData, writeRawData, skipRawData) are untagged.

### TextStream design
TextStream is an independent class (does not inherit from `std::ostream`). The formatting controls and encoding-awareness are different enough from `std::ostream` that inheritance creates more friction than value. This means all `operator<<`/`operator>>` overloads must be provided for TextStream explicitly — which is exactly what the migration section above tracks.
