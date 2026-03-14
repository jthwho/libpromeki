# Core Streams and Serialization

**Phase:** 2 (alongside IO abstractions)
**Dependencies:** Phase 1 (containers), Phase 2A (IODevice)
**Depends-on-this:** Phase 4 (pipeline frame serialization), Phase 7 (ObjectBase saveState/loadState)
**Standards:** All code must follow `CODING_STANDARDS.md`. Every class requires complete doctest unit tests. See `README.md` for full requirements.

Provides promeki-native stream classes for binary and text I/O. These present a Qt-style API and integrate with IODevice and promeki types. Once DataStream and TextStream are in place, all existing naked `std::` stream usage is migrated to use promeki streams.

---

## DataStream

Binary stream for structured, portable serialization. Primary use case: `ObjectBase::saveState()`/`loadState()`, file format I/O, network protocol encoding.

**Files:**
- [ ] `include/promeki/datastream.h`
- [ ] `src/datastream.cpp`
- [ ] `tests/datastream.cpp`

**Implementation checklist:**
- [ ] Header guard, includes, namespace
- [ ] Constructor from `IODevice *` — reads/writes to any IODevice
- [ ] Constructor from `Buffer *` — reads/writes to in-memory buffer
- [ ] Constructor from `Buffer &, OpenMode` — convenience for read-from or write-to buffer

### Versioning and byte order
- [ ] `enum ByteOrder { BigEndian, LittleEndian }`
- [ ] `void setByteOrder(ByteOrder)` — default: BigEndian (network byte order)
- [ ] `ByteOrder byteOrder() const`
- [ ] `void setVersion(int version)` — stream protocol version for forward/backward compat
- [ ] `int version() const`

### Status and error handling
- [ ] `enum Status { Ok, ReadPastEnd, ReadCorruptData, WriteFailed }`
- [ ] `Status status() const`
- [ ] `void resetStatus()`
- [ ] `bool atEnd() const`

### Write operators (operator<<)
- [ ] `DataStream &operator<<(int8_t)`
- [ ] `DataStream &operator<<(uint8_t)`
- [ ] `DataStream &operator<<(int16_t)`
- [ ] `DataStream &operator<<(uint16_t)`
- [ ] `DataStream &operator<<(int32_t)`
- [ ] `DataStream &operator<<(uint32_t)`
- [ ] `DataStream &operator<<(int64_t)`
- [ ] `DataStream &operator<<(uint64_t)`
- [ ] `DataStream &operator<<(float)`
- [ ] `DataStream &operator<<(double)`
- [ ] `DataStream &operator<<(bool)`
- [ ] `DataStream &operator<<(const String &)` — length-prefixed UTF-8
- [ ] `DataStream &operator<<(const Buffer &)` — length-prefixed raw bytes
- [ ] `DataStream &operator<<(const Variant &)` — type tag + value

### Read operators (operator>>)
- [ ] `DataStream &operator>>(int8_t &)`
- [ ] `DataStream &operator>>(uint8_t &)`
- [ ] `DataStream &operator>>(int16_t &)`
- [ ] `DataStream &operator>>(uint16_t &)`
- [ ] `DataStream &operator>>(int32_t &)`
- [ ] `DataStream &operator>>(uint32_t &)`
- [ ] `DataStream &operator>>(int64_t &)`
- [ ] `DataStream &operator>>(uint64_t &)`
- [ ] `DataStream &operator>>(float &)`
- [ ] `DataStream &operator>>(double &)`
- [ ] `DataStream &operator>>(bool &)`
- [ ] `DataStream &operator>>(String &)`
- [ ] `DataStream &operator>>(Buffer &)`
- [ ] `DataStream &operator>>(Variant &)`

### Raw byte access
- [ ] `ssize_t readRawData(void *buf, size_t len)`
- [ ] `ssize_t writeRawData(const void *buf, size_t len)`
- [ ] `ssize_t skipRawData(size_t len)`

### Promeki type serialization (operator<< and operator>>)

Add `DataStream` operators for existing promeki types. These can live in each type's header or in `datastream.h` — whichever avoids circular includes. Each type writes a deterministic binary representation.

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
- [ ] Doctest: round-trip all primitive types, round-trip String/Buffer, round-trip promeki types, byte order switching, version field, error status on truncated data

---

## TextStream

Formatted text I/O with encoding awareness. For human-readable output, config files, log formatting, and structured text parsing.

**Files:**
- [ ] `include/promeki/textstream.h`
- [ ] `src/textstream.cpp`
- [ ] `tests/textstream.cpp`

**Implementation checklist:**
- [ ] Header guard, includes, namespace
- [ ] Constructor from `IODevice *`
- [ ] Constructor from `Buffer *`
- [ ] Constructor from `String *` — read/write to String directly
- [ ] Constructor from `FILE *` — wrap C stdio (for stdout/stderr integration)

### Encoding
- [ ] `void setEncoding(const String &encoding)` — e.g., "UTF-8", "Latin-1"
- [ ] `String encoding() const`
- [ ] Default: UTF-8
- [ ] Encoding conversion on read/write (leverage String's encoding-aware backends)

### Write operations
- [ ] `TextStream &operator<<(const String &)`
- [ ] `TextStream &operator<<(const char *)`
- [ ] `TextStream &operator<<(char)`
- [ ] `TextStream &operator<<(int)` — formatted as decimal text
- [ ] `TextStream &operator<<(unsigned int)`
- [ ] `TextStream &operator<<(int64_t)`
- [ ] `TextStream &operator<<(uint64_t)`
- [ ] `TextStream &operator<<(float)`
- [ ] `TextStream &operator<<(double)`
- [ ] `TextStream &operator<<(bool)` — "true"/"false"
- [ ] `TextStream &operator<<(const Variant &)` — uses Variant's toString()

### Read operations
- [ ] `TextStream &operator>>(String &)` — reads whitespace-delimited token
- [ ] `TextStream &operator>>(char &)`
- [ ] `TextStream &operator>>(int &)`
- [ ] `TextStream &operator>>(int64_t &)`
- [ ] `TextStream &operator>>(double &)`
- [ ] `String readLine()` — read until newline
- [ ] `String readAll()` — read remaining content
- [ ] `String read(size_t maxLength)` — read up to N characters
- [ ] `bool atEnd() const`

### Formatting controls
- [ ] `enum FieldAlignment { Left, Right, Center }`
- [ ] `void setFieldWidth(int width)`
- [ ] `int fieldWidth() const`
- [ ] `void setFieldAlignment(FieldAlignment)`
- [ ] `void setPadChar(char c)` — default: space
- [ ] `void setIntegerBase(int base)` — 2, 8, 10, 16
- [ ] `int integerBase() const`
- [ ] `void setRealNumberPrecision(int precision)` — decimal places for floats
- [ ] `int realNumberPrecision() const`
- [ ] `enum RealNumberNotation { Fixed, Scientific, SmartNotation }`
- [ ] `void setRealNumberNotation(RealNumberNotation)`

### Manipulators (inline functions or sentinels)
- [ ] `endl` — writes newline and flushes
- [ ] `flush` — flushes underlying device
- [ ] `hex`, `dec`, `oct`, `bin` — change integer base
- [ ] `fixed`, `scientific` — change float notation
- [ ] `left`, `right`, `center` — change alignment

### Promeki type output
- [ ] Existing types with `operator<<(std::ostream &)` should also work with TextStream
- [ ] `Point`, `Rational`, `Size2D`, `Rect`, `Matrix3x3` — leverage existing toString() or stream operators

### Status
- [ ] `enum Status { Ok, ReadPastEnd, WriteFailed }`
- [ ] `Status status() const`
- [ ] `void resetStatus()`
- [ ] `void flush()`

### Doctest
- [ ] Round-trip text write/read for primitives
- [ ] Formatting: field width, alignment, pad char
- [ ] Integer base (hex, oct, bin)
- [ ] Float precision and notation
- [ ] Encoding conversion (UTF-8 <-> Latin-1)
- [ ] Read line, read all
- [ ] String target (write to String, read from String)

---

## ObjectBase Serialization (saveState / loadState)

**Phase:** 7 (enhanced existing classes) — depends on DataStream
**Dependencies:** DataStream

Add binary state serialization to ObjectBase. Each subclass can override to save/restore its own state. Uses DataStream for portable, versioned binary format.

**Files:**
- [ ] Modify `include/promeki/objectbase.h`
- [ ] Modify `src/objectbase.cpp`
- [ ] `tests/objectbase_serialization.cpp`

**Implementation checklist:**
- [ ] `virtual Error saveState(DataStream &stream) const` — default: saves nothing, returns Ok
- [ ] `virtual Error loadState(DataStream &stream)` — default: reads nothing, returns Ok
- [ ] `Error saveState(Buffer &buffer) const` — convenience: creates DataStream over buffer
- [ ] `Error loadState(const Buffer &buffer)` — convenience: creates DataStream over buffer
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

Once DataStream and TextStream are implemented, migrate all existing `std::` stream usage to promeki streams. This eliminates direct `std::` stream dependencies from the public API and internal implementation.

---

### AnsiStream Refactor

Currently extends `std::ostream` directly. Refactor to use `TextStream` as its backend.

**Files:**
- [ ] Modify `include/promeki/ansistream.h`
- [ ] Modify `src/ansistream.cpp`
- [ ] Update `tests/ansistream.cpp`

**Checklist:**
- [ ] Change base class from `std::ostream` to `TextStream` (or compose a `TextStream` internally)
- [ ] Constructor: take `TextStream &` or `IODevice *` instead of `std::ostream &`
- [ ] Preserve all existing ANSI escape methods (colors, cursor, screen control)
- [ ] `getCursorPosition()`: change `std::istream &input` parameter to use promeki stream or IODevice
- [ ] Remove `#include <iostream>` and `#include <ostream>`
- [ ] Update `tests/ansistream.cpp`: replace `std::ostringstream` with `TextStream` over `Buffer`

---

### StreamString Refactor

Currently extends `std::streambuf`. Refactor to work with `TextStream`.

**Files:**
- [ ] Modify `include/promeki/streamstring.h`
- [ ] Update `tests/streamstring.cpp`

**Checklist:**
- [ ] Remove `std::streambuf` inheritance
- [ ] Internal: accumulate into `String` buffer directly (keep newline callback behavior)
- [ ] `stream()` method: return `TextStream &` instead of `std::ostream &`
- [ ] Remove `#include <streambuf>` and `#include <ostream>`
- [ ] Update tests: replace `std::flush` / `std::endl` with TextStream equivalents

---

### Logger Refactor

Currently uses `std::ofstream` for file output and `std::cout` for console output.

**Files:**
- [ ] Modify `include/promeki/logger.h`
- [ ] Modify `src/logger.cpp`
- [ ] Update `tests/logger.cpp`

**Checklist:**
- [ ] Replace `std::ofstream _file` member with file-backed `TextStream` (over a File IODevice)
- [ ] Replace `std::cout` usage with `TextStream` over stdout (via `FILE *` constructor)
- [ ] Replace `std::endl` with TextStream `endl`
- [ ] Remove `#include <iostream>` and `#include <fstream>`
- [ ] Update `tests/logger.cpp`: replace `std::ifstream` reads with promeki File or TextStream

---

### operator<< / operator>> Migration

Replace all `std::ostream`/`std::istream` stream operators with `TextStream` equivalents.

**Files to modify:**

String (`include/promeki/string.h`, `src/string.cpp`):
- [ ] Replace `friend std::ostream &operator<<(std::ostream &, const String &)` with `TextStream &operator<<(TextStream &, const String &)`
- [ ] Replace `friend std::istream &operator>>(std::istream &, String &)` with `TextStream &operator>>(TextStream &, String &)`
- [ ] Replace internal `std::ostringstream` usage in `fromNumber()` methods with TextStream or `String::number()`
- [ ] Replace `std::istringstream` usage in `split()` and other parsing methods
- [ ] Remove `#include <iostream>` and `#include <sstream>`

Point (`include/promeki/point.h`):
- [ ] Replace `friend std::ostream &operator<<(std::ostream &, const Point &)` with TextStream variant
- [ ] Replace `friend std::istream &operator>>(std::istream &, Point &)` with TextStream variant
- [ ] Replace `std::stringstream` usage in `fromString()` and `toString()`

Size2D (`include/promeki/size2d.h`):
- [ ] Replace `friend std::ostream &operator<<` with TextStream variant
- [ ] Replace `friend std::istream &operator>>` with TextStream variant

Rect (`include/promeki/rect.h`):
- [ ] Replace `friend std::ostream &operator<<` with TextStream variant

Matrix3x3 (`include/promeki/matrix3x3.h`):
- [ ] Replace `friend std::ostream &operator<<` with TextStream variant
- [ ] Remove `#include <ostream>`

Rational (`include/promeki/rational.h`):
- [ ] Replace `friend std::ostream &operator<<` with TextStream variant
- [ ] Replace `std::stringstream` in `toString()` with TextStream over String
- [ ] Remove `#include <sstream>`

FourCC (`include/promeki/fourcc.h`):
- [ ] Remove `#include <iostream>` (audit if actually needed)

---

### Internal std::stringstream Replacement

Replace all internal `std::stringstream`/`std::istringstream`/`std::ostringstream` usage with TextStream over String/Buffer.

**Files:**

XYZColor (`include/promeki/xyzcolor.h`):
- [ ] Replace `std::stringstream` in `toString()` with TextStream over String
- [ ] Remove `#include <sstream>`

DateTime (`include/promeki/datetime.h`, `src/datetime.cpp`):
- [ ] Replace `std::istringstream` in `fromString()` with TextStream parsing
- [ ] Remove `#include <sstream>` from header

JSON (`include/promeki/json.h`):
- [ ] Replace `#include <sstream>` usage — audit and migrate to String operations or TextStream

System (`src/system.cpp`):
- [ ] Replace `#include <iostream>` — audit usage and migrate

MemPool (`src/mempool.cpp`):
- [ ] Replace `#include <iostream>` — audit usage and migrate

---

### TUI Stream Migration

**Files:**
- [ ] `src/tui/application.cpp`: replace `std::cout` in AnsiStream construction with TextStream/IODevice
- [ ] `src/tui/screen.cpp`: replace `std::flush` with TextStream flush
- [ ] `tests/tui/screen.cpp`: replace `std::ostringstream` with TextStream over Buffer

---

### Utils Migration

**Files:**
- [ ] `utils/promeki-info/main.cpp`: replace `std::cout` / `std::endl` with TextStream over stdout

---

### Test File Migration

After all library code is migrated, update tests that use `std::ostringstream` / `std::istringstream` as test harnesses:

- [ ] `tests/size2d.cpp` — replace `std::ostringstream`/`std::istringstream`
- [ ] `tests/matrix3x3.cpp` — replace `std::ostringstream`
- [ ] `tests/ansistream.cpp` — replace 9 instances of `std::ostringstream`
- [ ] `tests/logger.cpp` — replace `std::ifstream`
- [ ] `tests/string.cpp` — remove `#include <iostream>`
- [ ] `tests/streamstring.cpp` — replace `std::flush`/`std::endl`
- [ ] `tests/tui/screen.cpp` — replace `std::ostringstream`

---

### Verification

- [ ] `grep -r 'std::ostream\|std::istream\|std::iostream\|std::streambuf\|std::stringstream\|std::istringstream\|std::ostringstream\|std::ifstream\|std::ofstream\|std::fstream\|std::cout\|std::cerr\|std::cin\|std::endl\|std::flush' include/ src/ tests/ utils/ demos/` returns zero hits (excluding thirdparty)
- [ ] All tests pass after migration
- [ ] No `#include <iostream>`, `<ostream>`, `<istream>`, `<sstream>`, `<fstream>`, `<streambuf>` in non-thirdparty code

---

## Design Notes

### DataStream wire format
- All multi-byte integers: byte-order controlled by `setByteOrder()`, default big-endian
- Strings: `uint32_t` length prefix (byte count) + UTF-8 bytes (no null terminator)
- Buffers: `uint32_t` length prefix + raw bytes
- Lists/Maps/Sets: `uint32_t` count prefix + elements
- Floats/doubles: IEEE 754, byte-order swapped if needed

### TextStream design
TextStream is an independent class (does not inherit from `std::ostream`). The formatting controls and encoding-awareness are different enough from `std::ostream` that inheritance creates more friction than value. This means all `operator<<`/`operator>>` overloads must be provided for TextStream explicitly — which is exactly what the migration section above tracks.
